#include "panel.hpp"
#include "ids.h"
#include "palette.hpp"
#include "preset_drag.hpp"
#include "flcat/filter.hpp"
#include "flcat/paths.hpp"
#include "flcat/scanner.hpp"
#include "flcat/store.hpp"
#include <commctrl.h>
#include <uxtheme.h>
#include <shobjidl.h>
#include <set>
#include <memory>
#include <commdlg.h>
#include <cmath>
#include <algorithm>

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace fs = std::filesystem;
namespace {
using namespace palette;
HBRUSH color_brush(HDC dc,COLORREF color) {
    SetDCBrushColor(dc,color);
    return static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
}
void fill(HDC dc,RECT rect,COLORREF color) { FillRect(dc,&rect,color_brush(dc,color)); }
void rounded(HDC dc,RECT r,int radius,COLORREF body,COLORREF outline) {
    int saved=SaveDC(dc);
    SelectObject(dc,GetStockObject(DC_BRUSH)); SetDCBrushColor(dc,body);
    SelectObject(dc,GetStockObject(DC_PEN)); SetDCPenColor(dc,outline);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,2*radius,2*radius);
    RestoreDC(dc,saved);
}
fs::path database() { return flcat::data_home() / "FL Plugin Atlas/catalog.sqlite3"; }
std::wstring wide(const std::string& s) { return fs::u8path(s).wstring(); }
std::string utf8(const std::wstring& s) { return fs::path(s).u8string(); }
std::wstring field(HWND w, int id) {
    HWND item = GetDlgItem(w,id);
    std::wstring text(GetWindowTextLengthW(item)+1,0);
    text.resize(GetWindowTextW(item,text.data(),static_cast<int>(text.size())));
    return text;
}
std::vector<int> selected(HWND list) {
    const int n = static_cast<int>(SendMessageW(list,LB_GETSELCOUNT,0,0));
    std::vector<int> indices(n > 0 ? n : 0);
    if (n > 0) SendMessageW(list,LB_GETSELITEMS,n,reinterpret_cast<LPARAM>(indices.data()));
    return indices;
}
std::wstring label(HWND list, int index) {
    auto len=SendMessageW(list,LB_GETTEXTLEN,index,0);
    if (len==LB_ERR) return {};
    std::wstring value(len+1,0);
    SendMessageW(list,LB_GETTEXT,index,reinterpret_cast<LPARAM>(value.data()));
    value.resize(len);
    return value;
}
void status(HWND w,const std::wstring& s) { SetDlgItemTextW(w,STATUS,s.c_str()); }

// Line icons share a 16-unit grid, including the search-field affordance.
void icon(HDC dc,int id,int x,int y,int size,COLORREF color) {
    int saved=SaveDC(dc);
    auto pen=CreatePen(PS_SOLID,std::max(1,size/10),color);
    SelectObject(dc,pen); SelectObject(dc,GetStockObject(NULL_BRUSH));
    auto X=[&](int n){return x+MulDiv(n,size,16);};
    auto Y=[&](int n){return y+MulDiv(n,size,16);};
    auto line=[&](int a,int b,int c,int d){MoveToEx(dc,X(a),Y(b),nullptr);LineTo(dc,X(c),Y(d));};
    if(id==SEARCH_ICON) {
        Ellipse(dc,X(1),Y(1),X(11),Y(11)); line(10,10,15,15);
    } else if(id==BROWSE) {
        POINT folder[]={{X(1),Y(4)},{X(6),Y(4)},{X(8),Y(6)},{X(15),Y(6)},
                        {X(15),Y(13)},{X(1),Y(13)},{X(1),Y(4)}};
        Polyline(dc,folder,7);
    } else if(id==FILL_IMAGES) {
        line(8,1,8,11); line(4,7,8,11); line(8,11,12,7);
        line(1,11,1,15); line(1,14,15,14); line(15,15,15,11);
    } else if(id==IMAGE_PICK) {
        Rectangle(dc,X(1),Y(2),X(15),Y(14)); line(2,12,6,8); line(6,8,9,11); line(9,11,14,6);
    } else if(id==ADD_PLUGIN) {
        line(8,2,8,14); line(2,8,14,8);
    } else if(id==SCAN) {
        for(int a:{2,10}) {line(a,2,a+4,2);line(a,14,a+4,14);}
        for(int a:{2,14}) {line(a,2,a,6);line(a,10,a,14);}
        line(4,8,12,8);
    }
    RestoreDC(dc,saved);DeleteObject(pen);
}

LRESULT CALLBACK edit_hint(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_KEYDOWN && wp==VK_ESCAPE && GetDlgCtrlID(w)==SEARCH_PLUGINS) {
        SetWindowTextW(w,L""); return 0;
    }
    auto result=DefSubclassProc(w,msg,wp,lp);
    if((msg==WM_PAINT || msg==WM_PRINTCLIENT || msg==WM_PRINT) && !GetWindowTextLengthW(w) && GetFocus()!=w) {
        HDC dc=msg==WM_PAINT?GetDC(w):reinterpret_cast<HDC>(wp);
        if(dc) {
            auto saved=SaveDC(dc);
            RECT box{}; GetClientRect(w,&box);
            SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(w,WM_GETFONT,0,0)));
            SetTextColor(dc,muted);SetBkMode(dc,TRANSPARENT);
            DrawTextW(dc,GetDlgCtrlID(w)==VALUE?L"输入用途或厂商名":L"搜索插件或厂商",-1,&box,
                      DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
            RestoreDC(dc,saved);
            if(msg==WM_PAINT) ReleaseDC(w,dc);
        }
    }
    if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS) {
        InvalidateRect(w,nullptr,TRUE);
        RedrawWindow(GetParent(w),nullptr,nullptr,RDW_INVALIDATE|RDW_NOCHILDREN);
    }
    return result;
}

// Small marks are drawn at the control's actual size so they follow host DPI.
void signal_mark(HDC dc,RECT box,COLORREF color,bool keys,COLORREF backing) {
    int w=box.right-box.left,h=box.bottom-box.top,cy=box.top+h/2;
    int gap=std::max(1,w/15), count=keys?4:5;
    int width=std::max(1,(w-(count-1)*gap)/count);
    const double peaks[]={.28,.59,.95,.54,.34};
    int x=box.left;
    for(int i=0;i<count;++i,x+=width+gap) {
        int bar=keys?h*2/3:std::max(2,int(h*peaks[i]));
        fill(dc,{x,cy-bar/2,x+width,cy+(bar+1)/2},color);
        if(keys && i!=2)
            fill(dc,{x+width-1,cy-bar/2,x+width+gap,cy},backing);
    }
}

LRESULT CALLBACK vendor_face(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    if(msg!=WM_PAINT && msg!=WM_PRINTCLIENT && msg!=WM_PRINT) return DefSubclassProc(w,msg,wp,lp);
    PAINTSTRUCT ps{};
    HDC dc=msg==WM_PAINT?BeginPaint(w,&ps):reinterpret_cast<HDC>(wp);
    if(dc) {
        int saved=SaveDC(dc);
        RECT r{}; GetClientRect(w,&r);
        reinterpret_cast<StudioSurface*>(data)->paint(dc,w,GetParent(w),r);
        int h=r.bottom;
        rounded(dc,r,5,input_surface,GetFocus()==w?focus:edge);
        int x=r.right-h/2, y=h/2;
        SelectObject(dc,GetStockObject(DC_PEN)); SetDCPenColor(dc,muted);
        MoveToEx(dc,x-h/7,y-h/15,nullptr); LineTo(dc,x,y+h/12); LineTo(dc,x+h/7+1,y-h/15-1);
        r.left+=h/3; r.right-=h;
        auto value=field(GetParent(w),VENDORS);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,ink);
        SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(w,WM_GETFONT,0,0)));
        DrawTextW(dc,value.c_str(),-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        RestoreDC(dc,saved);
    }
    if(msg==WM_PAINT) EndPaint(w,&ps);
    return 0;
}

}

bool Panel::create() {
    return CreateDialogParamW(reinterpret_cast<HINSTANCE>(&__ImageBase), MAKEINTRESOURCEW(ATLAS_PANEL),
        GetDesktopWindow(),dialog,reinterpret_cast<LPARAM>(this)) != nullptr;
}
void Panel::close() {
    cancel=true;
    stop_images=true;
    if(image_job.valid()) image_job.wait();
    image_job={};
    if (job.valid()) job.wait();
    job = {};
    if (window) {KillTimer(window,3);DestroyWindow(window);}
    window=nullptr;
    DeleteObject(heading); heading=nullptr;
    DeleteObject(bold); bold=nullptr;
    DeleteObject(display); display=nullptr;
    DeleteObject(mono); mono=nullptr;
    preview_index.reset(); image={}; previous_image={}; preview_blend.reset(1);
    surface.clear();
    if(buffered_paint) BufferedPaintUnInit();
    buffered_paint=false;
}
INT_PTR CALLBACK Panel::dialog(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto* panel=reinterpret_cast<Panel*>(GetWindowLongPtrW(w,DWLP_USER));
    try {
        if (msg==WM_INITDIALOG) {
            panel=reinterpret_cast<Panel*>(lp);
            panel->window=w;
            SetWindowLongPtrW(w,DWLP_USER,lp);
            panel->init();
            return TRUE;
        }
        if (!panel) return FALSE;
        if(msg==WM_PAINT || msg==WM_PRINTCLIENT) {
            PAINTSTRUCT ps{};
            HDC dc=msg==WM_PAINT?BeginPaint(w,&ps):reinterpret_cast<HDC>(wp);
            RECT area{};GetClientRect(w,&area);
            panel->surface.paint(dc,w,w,area);
            int focused=GetDlgCtrlID(GetFocus());
            RECT ring=focused==SEARCH_PLUGINS?RECT{154,107,420,134}:RECT{88,372,304,391};
            if(focused==SEARCH_PLUGINS || focused==VALUE) {
                MapDialogRect(w,&ring);
                auto saved=SaveDC(dc);
                SelectObject(dc,GetStockObject(NULL_BRUSH));
                SelectObject(dc,GetStockObject(DC_PEN));SetDCPenColor(dc,focus);
                RoundRect(dc,ring.left,ring.top,ring.right,ring.bottom,8,8);
                RestoreDC(dc,saved);
            }
            if(msg==WM_PAINT) EndPaint(w,&ps);
            return TRUE;
        }
        if(msg==WM_CTLCOLORDLG || msg==WM_CTLCOLORSTATIC || msg==WM_CTLCOLORLISTBOX || msg==WM_CTLCOLOREDIT) {
            auto dc=reinterpret_cast<HDC>(wp);
            int id=GetDlgCtrlID(reinterpret_cast<HWND>(lp));
            bool input=msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX;
            auto bg=input?input_surface:background;
            SetBkColor(dc,bg);
            SetTextColor(dc,id==TITLE || id==PREVIEW_NAME || input ? ink : muted);
            if(msg==WM_CTLCOLOREDIT) return reinterpret_cast<INT_PTR>(color_brush(dc,bg));
            if(msg==WM_CTLCOLORLISTBOX && GetDlgCtrlID(reinterpret_cast<HWND>(lp))!=RESULTS &&
                GetDlgCtrlID(reinterpret_cast<HWND>(lp))!=PURPOSES)
                return reinterpret_cast<INT_PTR>(color_brush(dc,input_surface));
            SetBkMode(dc,TRANSPARENT);
            return reinterpret_cast<INT_PTR>(panel->surface.brush_for(dc,reinterpret_cast<HWND>(lp),w));
        }
        if(msg==WM_DRAWITEM) {panel->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;}
        if (msg==WM_NCDESTROY) panel->window=nullptr;
        if (msg==WM_TIMER && wp==1) { panel->complete(); return TRUE; }
        if (msg==WM_TIMER && wp==2) { panel->finish_images(); return TRUE; }
        if (msg==WM_TIMER && wp==3) { panel->animate_preview(); return TRUE; }
        if(msg==WM_SETTINGCHANGE) {
            for(int id:{BROWSE,FILL_IMAGES,SCAN,ADD_PLUGIN,IMAGE_PICK,IMAGE_RESET,EDIT_MENU,TYPE_ALL,TYPE_EFFECTS,TYPE_INSTRUMENTS})
                SendDlgItemMessageW(w,id,msg,wp,lp);
            panel->purposes_scroll.settings_changed();
            panel->results_scroll.settings_changed();
            if(!motion::enabled()) {
                panel->preview_blend.finish(); panel->previous_image={}; KillTimer(w,3);
                InvalidateRect(GetDlgItem(w,PREVIEW),nullptr,FALSE);
            }
        }
        if (msg!=WM_COMMAND) return FALSE;
        const auto id=LOWORD(wp);
        if (id==SCAN && HIWORD(wp)==BN_CLICKED) panel->scan();
        else if(id==SEARCH_ICON && HIWORD(wp)==STN_CLICKED) SetFocus(GetDlgItem(w,SEARCH_PLUGINS));
        else if(id==SEARCH_PLUGINS && HIWORD(wp)==EN_CHANGE) panel->filter();
        else if(id>=TYPE_ALL && id<=TYPE_INSTRUMENTS && HIWORD(wp)==BN_CLICKED) {
            panel->type_filter=id-TYPE_ALL;
            for(int control:{TYPE_ALL,TYPE_EFFECTS,TYPE_INSTRUMENTS})
                InvalidateRect(GetDlgItem(w,control),nullptr,FALSE);
            panel->filter();
        }
        else if(id==RESULTS && HIWORD(wp)==LBN_SELCHANGE) {
            auto list=GetDlgItem(w,RESULTS);
            auto sel=selected(list);
            int focus=static_cast<int>(SendMessageW(list,LB_GETCARETINDEX,0,0));
            if(!sel.empty() && SendMessageW(list,LB_GETSEL,focus,0)<=0) focus=sel.front();
            panel->preview(sel.empty()?std::nullopt:std::optional<size_t>(panel->visible.at(focus)));
            panel->selection_changed();
        }
        else if((id==IMAGE_PICK || id==IMAGE_RESET) && HIWORD(wp)==BN_CLICKED) panel->choose_image(id==IMAGE_RESET);
        else if (id==BROWSE && HIWORD(wp)==BN_CLICKED) panel->browse();
        else if (id==VENDORS && HIWORD(wp)==CBN_SELCHANGE) panel->filter();
        else if (id==PURPOSES && HIWORD(wp)==LBN_SELCHANGE) panel->filter();
        else if (id==ADD_PLUGIN && HIWORD(wp)==BN_CLICKED && panel->preview_index) {
            const auto& e=panel->entries.at(*panel->preview_index);
            status(w,e.kind=="Effects"?L"点击 Mixer 空效果插槽，Esc 取消":L"点击 Channel Rack 空白处，Esc 取消");
            UpdateWindow(GetDlgItem(w,STATUS));
            bool sent=place_preset(w,fs::u8path(panel->library_id),fs::u8path(e.path));
            if(IsWindow(w)) status(w,sent?L"预设已交给 FL Studio":L"已取消，或目标未接收预设");
        }
        else if(id==FILL_IMAGES && HIWORD(wp)==BN_CLICKED) panel->fill_images();
        else if (id==EDIT_MENU && HIWORD(wp)==BN_CLICKED) {
            HMENU menu=CreatePopupMenu();
            if(!menu) throw std::runtime_error("无法打开分类菜单。");
            AppendMenuW(menu,MF_STRING,ADD_TAG,L"添加用途");
            AppendMenuW(menu,MF_STRING,REMOVE_TAG,L"移除用途");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING,SET_VENDOR,L"修正厂商");
            AppendMenuW(menu,MF_STRING,ALIAS_VENDOR,L"合并厂商别名");
            RECT bounds{}; GetWindowRect(GetDlgItem(w,EDIT_MENU),&bounds);
            auto action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_BOTTOMALIGN,
                bounds.right,bounds.top,0,w,nullptr);
            DestroyMenu(menu);
            if(action) panel->edit(action);
        }
        else return FALSE;
        return TRUE;
    } catch(const std::exception& e) {
        try { status(w,wide(e.what())); } catch(...) { status(w,L"操作失败"); }
    } catch(...) { status(w,L"操作失败"); }
    return TRUE;
}
void Panel::init() {
    buffered_paint=SUCCEEDED(BufferedPaintInit());
    surface.build(window);
    type_filter=0;
    ShowWindow(GetDlgItem(window,EMPTY_RESULTS),SW_HIDE);
    LOGFONTW font{};
    GetObjectW(reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)),sizeof(font),&font);
    font.lfQuality=CLEARTYPE_QUALITY;
    font.lfWeight=FW_SEMIBOLD;
    bold=CreateFontIndirectW(&font);
    SendDlgItemMessageW(window,LIST_HEADING,WM_SETFONT,reinterpret_cast<WPARAM>(bold),FALSE);
    int baseHeight=font.lfHeight;
    font.lfHeight=baseHeight*16/10;
    display=CreateFontIndirectW(&font);
    SendDlgItemMessageW(window,PREVIEW_NAME,WM_SETFONT,reinterpret_cast<WPARAM>(display),FALSE);
    SendDlgItemMessageW(window,LIST_HEADING,WM_SETFONT,reinterpret_cast<WPARAM>(display),FALSE);
    wcscpy_s(font.lfFaceName,L"Segoe UI");
    font.lfHeight=baseHeight*20/10;
    heading=CreateFontIndirectW(&font);
    SendDlgItemMessageW(window,TITLE,WM_SETFONT,reinterpret_cast<WPARAM>(heading),FALSE);
    wcscpy_s(font.lfFaceName,L"Consolas"); font.lfHeight=MulDiv(baseHeight,9,10); font.lfWeight=FW_NORMAL;
    mono=CreateFontIndirectW(&font);
    SendDlgItemMessageW(window,PREVIEW_PATH,WM_SETFONT,reinterpret_cast<WPARAM>(mono),FALSE);
    RECT units{0,0,1,30}; MapDialogRect(window,&units);
    RECT listArea{}; GetClientRect(GetDlgItem(window,RESULTS),&listArea);
    // Fit whole rows at the resource's scaled height.
    // Resource mapping rounds each edge separately; round the row count too.
    // Otherwise 180 dialog units / 30 units can become five rows at 125% DPI.
    int rows=std::max(1L,(listArea.bottom+units.bottom/2)/units.bottom);
    auto rowHeight=listArea.bottom/rows;
    for(int id:{RESULTS,RESULT_SCROLL}) {
        auto child=GetDlgItem(window,id); RECT box{}; GetClientRect(child,&box);
        SetWindowPos(child,nullptr,0,0,box.right,rows*rowHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    SendDlgItemMessageW(window,RESULTS,LB_SETITEMHEIGHT,0,rowHeight);
    SendDlgItemMessageW(window,PURPOSES,LB_SETITEMHEIGHT,0,units.bottom*3/5);
    SetWindowSubclass(GetDlgItem(window,VENDORS),vendor_face,1,reinterpret_cast<DWORD_PTR>(&surface));
    for(int id:{PURPOSES,RESULTS,VENDORS}) SetWindowTheme(GetDlgItem(window,id),L"DarkMode_Explorer",nullptr);
    size_t button=0;
    for(int id:{BROWSE,FILL_IMAGES,SCAN,ADD_PLUGIN,IMAGE_PICK,IMAGE_RESET,EDIT_MENU,TYPE_ALL,TYPE_EFFECTS,TYPE_INSTRUMENTS})
        buttons[button++].attach(GetDlgItem(window,id));
    for(int id:{SEARCH_PLUGINS,VALUE}) SetWindowSubclass(GetDlgItem(window,id),edit_hint,1,0);
    purposes_scroll.attach(GetDlgItem(window,PURPOSES),GetDlgItem(window,PURPOSE_SCROLL));
    results_scroll.attach(GetDlgItem(window,RESULTS),GetDlgItem(window,RESULT_SCROLL));
    auto path=flcat::documents() / "Image-Line/FL Studio/Presets/Plugin database";
    SetDlgItemTextW(window,LIBRARY,path.c_str());
    library_id=flcat::path_id(fs::weakly_canonical(path));
    if (fs::exists(database())) entries=flcat::Store(database()).list(library_id);
    load_filters();
    if(entries.empty()) status(window,L"点击“扫描插件”读取默认库，或选择其他目录。");
}
void Panel::browse() {
    IFileOpenDialog* raw=nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&raw))))
        throw std::runtime_error("无法打开目录选择器。");
    auto release=[](auto* p){p->Release();};
    std::unique_ptr<IFileOpenDialog,decltype(release)> dlg(raw,release);
    DWORD flags=0;
    dlg->GetOptions(&flags);
    dlg->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
    IShellItem* current=nullptr;
    if(SUCCEEDED(SHCreateItemFromParsingName(field(window,LIBRARY).c_str(),nullptr,IID_PPV_ARGS(&current)))) {
        dlg->SetFolder(current);
        current->Release();
    }
    if (dlg->Show(window)!=S_OK) return;
    IShellItem* item=nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
        PWSTR name=nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&name))) {
            SetDlgItemTextW(window,LIBRARY,name);
            CoTaskMemFree(name);
            item->Release();
            scan();
            return;
        }
        item->Release();
    }
}
void Panel::scan() {
    if(job.valid()) return;
    auto text=field(window,LIBRARY);
    if(text.empty()) throw std::runtime_error("请选择插件库目录。");
    auto path=fs::weakly_canonical(fs::absolute(fs::path(text)));
    auto id=flcat::path_id(path);
    cancel=false;
    if (!SetTimer(window,1,120,nullptr)) throw std::runtime_error("无法启动扫描。");
    job=std::async(std::launch::async,[this,path,id] {
        auto rows=flcat::scan(path,[this]{return cancel.load();});
        if(cancel) throw std::runtime_error("已取消扫描。");
        flcat::Store store(database());
        store.replace_scan(id,rows);
        return store.list(id);
    });
    stop_images=true;
    EnableWindow(GetDlgItem(window,FILL_IMAGES),FALSE);
    for(int control : {SCAN,BROWSE,LIBRARY,EDIT_MENU})
        EnableWindow(GetDlgItem(window,control),FALSE);
    status(window,L"正在扫描插件库…");
}
void Panel::complete() {
    if(!job.valid() || job.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    KillTimer(window,1);
    EnableWindow(GetDlgItem(window,FILL_IMAGES),TRUE);
    for(int control : {SCAN,BROWSE,LIBRARY,EDIT_MENU})
        EnableWindow(GetDlgItem(window,control),TRUE);
    auto rows=job.get();
    library_id=flcat::path_id(fs::weakly_canonical(fs::absolute(fs::path(field(window,LIBRARY)))));
    entries=std::move(rows);
    load_filters();
    fill_images();
}
void Panel::fill_images() {
    if(entries.empty() || job.valid()) return;
    if(image_job.valid()) return;
    stop_images=false;
    if(!SetTimer(window,2,350,nullptr)) throw std::runtime_error("无法启动图片任务。");
    image_job=std::async(std::launch::async,[this,rows=entries,root=fs::u8path(library_id)] {
        return artwork::fill(root,rows,stop_images);
    });
    EnableWindow(GetDlgItem(window,FILL_IMAGES),FALSE);
    status(window,L"正在按厂商和插件名查找缺图…");
}
void Panel::finish_images() {
    if(!image_job.valid() || image_job.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
    KillTimer(window,2);
    EnableWindow(GetDlgItem(window,FILL_IMAGES),!job.valid());
    auto report=image_job.get();
    if(stop_images) {if(!job.valid()) fill_images(); return;}
    preview(preview_index);
    status(window,L"新增 "+std::to_wstring(report.saved)+L" 张 · 未匹配 "+std::to_wstring(report.missing)+
           L" · 失败 "+std::to_wstring(report.errors));
}
void Panel::load_filters() {
    std::set<std::string> vendors,tags;
    for(auto& e:entries) if(e.available) {
        vendors.insert(e.vendor.empty()?flcat::unknown_vendor:e.vendor);
        if(e.tags.empty()) tags.insert(flcat::unclassified);
        for(auto& t:e.tags) tags.insert(t.first);
    }
    auto previous=field(window,VENDORS);
    auto list=GetDlgItem(window,PURPOSES);
    int old_index=static_cast<int>(SendMessageW(list,LB_GETCURSEL,0,0));
    auto keep=old_index>0 ? label(list,old_index) : std::wstring{};
    auto send=[this](int id,UINT m,const std::wstring& s) {
        return SendDlgItemMessageW(window,id,m,0,reinterpret_cast<LPARAM>(s.c_str()));
    };
    SendDlgItemMessageW(window,VENDORS,CB_RESETCONTENT,0,0);
    send(VENDORS,CB_ADDSTRING,L"全部厂商");
    int index=0;
    for(auto& v:vendors) { auto s=wide(v); auto n=send(VENDORS,CB_ADDSTRING,s); if(s==previous) index=static_cast<int>(n); }
    SendDlgItemMessageW(window,VENDORS,CB_SETCURSEL,index,0);
    SendMessageW(list,LB_RESETCONTENT,0,0);
    send(PURPOSES,LB_ADDSTRING,L"全部");
    int purpose=0;
    for(auto& t:tags) {
        auto s=wide(t);
        auto n=send(PURPOSES,LB_ADDSTRING,s);
        if(s==keep && n>=0) purpose=static_cast<int>(n);
    }
    SendMessageW(list,LB_SETCURSEL,purpose,0);
    filter();
}
void Panel::filter() {
    flcat::Filter f;
    auto query=field(window,SEARCH_PLUGINS);
    if(type_filter) f.kind=type_filter==1?"Effects":"Generators";
    if(SendDlgItemMessageW(window,VENDORS,CB_GETCURSEL,0,0)>0) f.vendor=utf8(field(window,VENDORS));
    HWND purposes=GetDlgItem(window,PURPOSES);
    int purpose=static_cast<int>(SendMessageW(purposes,LB_GETCURSEL,0,0));
    if(purpose>0) f.tags.push_back(utf8(label(purposes,purpose)));
    auto list=GetDlgItem(window,RESULTS);
    SendMessageW(list,WM_SETREDRAW,FALSE,0);
    SendMessageW(list,LB_RESETCONTENT,0,0);
    visible.clear();
    purpose_counts.clear();
    auto counted=f;
    counted.tags.clear();
    for(size_t i=0;i<entries.size();++i) {
        const auto& e=entries[i];
        if(!counted.matches(e)) continue;
        auto name=wide(e.name), vendor=wide(e.vendor.empty()?flcat::unknown_vendor:e.vendor);
        auto contains=[&](const std::wstring& s) {
            return !s.empty() && FindNLSStringEx(LOCALE_NAME_INVARIANT,FIND_FROMSTART|NORM_IGNORECASE,
                s.data(),int(s.size()),query.data(),int(query.size()),nullptr,nullptr,nullptr,0)>=0;
        };
        if(!query.empty() && !contains(name) && !contains(vendor)) continue;
        ++purpose_counts[""];
        if(e.tags.empty()) ++purpose_counts[flcat::unclassified];
        for(const auto& tag:e.tags) ++purpose_counts[tag.first];
        if(!f.matches(e)) continue;
        auto text=name+L"   ·   "+vendor+L"   ·   ";
        if(e.tags.empty()) text+=wide(flcat::unclassified);
        for(auto it=e.tags.begin();it!=e.tags.end();++it) { if(it!=e.tags.begin()) text+=L"、"; text+=wide(it->first); }
        if(SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()))>=0) visible.push_back(i);
    }
    if(!visible.empty()) SendMessageW(list,LB_SETSEL,TRUE,0);
    preview(visible.empty()?std::nullopt:std::optional<size_t>(visible.front()));
    SendMessageW(list,WM_SETREDRAW,TRUE,0);
    InvalidateRect(list,nullptr,TRUE);
    InvalidateRect(GetDlgItem(window,PURPOSES),nullptr,FALSE);
    ShowWindow(GetDlgItem(window,EMPTY_RESULTS),visible.empty()?SW_SHOWNOACTIVATE:SW_HIDE);
    InvalidateRect(GetDlgItem(window,EMPTY_RESULTS),nullptr,FALSE);
    SetDlgItemTextW(window,RESULT_COUNT,(std::to_wstring(visible.size())+L" 个预设入口").c_str());
    selection_changed();
    if(!job.valid() && !image_job.valid()) status(window,L"");
}
void Panel::selection_changed() {
    auto count=selected(GetDlgItem(window,RESULTS)).size();
    auto caption=L"已选 "+std::to_wstring(count)+L" 个预设   ·   Ctrl / Shift 多选";
    SetDlgItemTextW(window,SELECTION_COUNT,caption.c_str());
    EnableWindow(GetDlgItem(window,EDIT_MENU),count>0 && !job.valid());
}
void Panel::edit(int command) {
    auto value=utf8(field(window,VALUE));
    if(value.find_first_not_of(" \t\r\n")==std::string::npos) throw std::runtime_error("请先填写用途或厂商名称。");
    if(command==ADD_TAG && value==flcat::unclassified) throw std::runtime_error("“待分类”是自动分组。");
    flcat::Store store(database());
    if(command==ALIAS_VENDOR) {
        if(SendDlgItemMessageW(window,VENDORS,CB_GETCURSEL,0,0)<=0) throw std::runtime_error("先选择要合并的厂商。");
        store.alias_vendor(library_id,utf8(field(window,VENDORS)),value);
    } else {
        std::set<std::string> keys;
        for(int n:selected(GetDlgItem(window,RESULTS))) keys.insert(entries.at(visible.at(n)).plugin_key);
        if(keys.empty()) throw std::runtime_error("请先选中一个或多个预设入口。");
        if(command==SET_VENDOR) {
            if(keys.size()!=1) throw std::runtime_error("修正厂商时请选择一个插件。");
            store.set_vendor(library_id,*keys.begin(),value);
        } else {
            std::vector<std::string> add,remove;
            (command==ADD_TAG?add:remove).push_back(value);
            store.edit_tags(library_id,{keys.begin(),keys.end()},add,remove);
        }
    }
    entries=store.list(library_id);
    load_filters();
    status(window,L"分类修改已保存");
}

void Panel::preview(std::optional<size_t> index) {
    bool changed=index!=preview_index;
    if(changed && preview_index && motion::enabled() && IsWindowVisible(window))
        previous_image=PreviewImage::capture(GetDlgItem(window,PREVIEW),5);
    else previous_image={};
    preview_blend.reset(1);KillTimer(window,3);
    preview_index=index;
    image={};
    const auto* e=index?&entries.at(*index):nullptr;
    std::wstring source,meta,path;
    if(e) {
        if(!e->custom_image.empty() && image.load(fs::u8path(e->custom_image)))
            source=L"自选图片";
        auto try_preset=[&](const flcat::Entry& preset) {
            if(!image.pixels.empty()) return;
            try {
                for(const auto& file:flcat::thumbnail_files(fs::u8path(library_id),preset.path))
                    if(image.load(file)){ source=L"FL 缩略图"; break; }
            } catch(const std::exception&) { /* A missing sidecar does not invalidate the catalog. */ }
        };
        try_preset(*e);
        if(image.pixels.empty() && !e->identity_conflict)
            for(const auto& sibling:entries)
                if(sibling.available && !sibling.identity_conflict && sibling.plugin_key==e->plugin_key && sibling.path!=e->path) {
                    try_preset(sibling);
                    if(!image.pixels.empty()) break;
                }
        if(source.empty()) {
            auto cache=artwork::cached(*e);
            if(!cache.empty() && image.load(cache)) source=L"网络产品图";
            else source=L"识别封面";
        }
        meta=wide(e->vendor.empty()?flcat::unknown_vendor:e->vendor)+L"  /  "+
             (e->kind=="Effects"?L"效果器":L"乐器");
        if(!e->format.empty()) meta+=L"  /  "+wide(e->format);
        path=wide(e->path);
    }
    SetDlgItemTextW(window,PREVIEW_NAME,e?wide(e->name).c_str():L"未选择插件");
    if(!e) meta=entries.empty()?L"扫描插件后可查看预览":visible.empty()?L"试试其他关键词、厂商或用途":L"在列表中选择插件以查看预览";
    SetDlgItemTextW(window,PREVIEW_META,meta.c_str());
    SetDlgItemTextW(window,PREVIEW_PATH,path.c_str());
    SetDlgItemTextW(window,PREVIEW_SOURCE,source.c_str());
    std::wstring tags;
    if(e) {
        for(const auto& tag:e->tags) { if(!tags.empty()) tags+=L"  /  "; tags+=wide(tag.first); }
        if(tags.empty()) tags=wide(flcat::unclassified);
    }
    SetDlgItemTextW(window,PREVIEW_TAGS,tags.c_str());
    EnableWindow(GetDlgItem(window,ADD_PLUGIN),e!=nullptr);
    EnableWindow(GetDlgItem(window,PREVIEW),e!=nullptr);
    EnableWindow(GetDlgItem(window,IMAGE_PICK),e!=nullptr);
    EnableWindow(GetDlgItem(window,IMAGE_RESET),e && !e->custom_image.empty());
    if(e && image.pixels.empty() && !previous_image.pixels.empty())
        image=PreviewImage::capture(GetDlgItem(window,PREVIEW),5);
    if(changed && !previous_image.pixels.empty() && !image.pixels.empty() && motion::enabled() && IsWindowVisible(window)) {
        preview_blend.reset(0);preview_blend.to(1,motion::preview_ms,true);
        if(!SetTimer(window,3,motion::frame_ms,nullptr)) preview_blend.finish();
    }
    if(!preview_blend.moving()) previous_image={};
    InvalidateRect(GetDlgItem(window,PREVIEW),nullptr,FALSE);
}

void Panel::animate_preview() {
    preview_blend.tick(GetTickCount64());
    if(!motion::enabled() || !IsWindowVisible(window)) preview_blend.finish();
    if(!preview_blend.moving()) {KillTimer(window,3);previous_image={};}
    InvalidateRect(GetDlgItem(window,PREVIEW),nullptr,FALSE);
}

void Panel::choose_image(bool reset) {
    if(!preview_index) return;
    auto& current=entries.at(*preview_index);
    std::string path;
    if(!reset) {
        std::wstring file(32768,L'\0');
        OPENFILENAMEW dialog{};
        dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=window;
        dialog.lpstrFile=file.data(); dialog.nMaxFile=static_cast<DWORD>(file.size());
        dialog.lpstrFilter=L"插件图片 (PNG, JPEG, BMP)\0*.png;*.jpg;*.jpeg;*.bmp\0\0";
        dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
        if(!GetOpenFileNameW(&dialog)) return;
        file.resize(wcslen(file.c_str()));
        PreviewImage check;
        if(!check.load(file)) throw std::runtime_error("无法打开图片，请选择有效的 PNG、JPEG 或 BMP 图片（不超过 6400 万像素）。");
        path=utf8(file);
    }
    flcat::Store(database()).set_image(library_id,current.plugin_key,path);
    for(auto& e:entries) if(e.plugin_key==current.plugin_key) e.custom_image=path;
    preview(preview_index);
}

void Panel::draw(const DRAWITEMSTRUCT& item) {
    HDC dc=item.hDC;
    HPAINTBUFFER buffer=nullptr;
    if(buffered_paint) buffer=BeginBufferedPaint(item.hDC,&item.rcItem,BPBF_COMPATIBLEBITMAP,nullptr,&dc);
    if(!buffer) dc=item.hDC;
    RECT r=item.rcItem;
    int saved=SaveDC(dc);
    SetBkMode(dc,TRANSPARENT);
    auto base=reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0));
    auto text=[&](const std::wstring& value,RECT box,COLORREF color,HFONT font=nullptr,UINT flags=DT_LEFT) {
        SelectObject(dc,font?font:base); SetTextColor(dc,color);
        auto ellipsis=item.CtlID==PREVIEW_PATH?DT_PATH_ELLIPSIS:DT_END_ELLIPSIS;
        DrawTextW(dc,value.c_str(),int(value.size()),&box,flags|DT_NOPREFIX|ellipsis|DT_SINGLELINE|DT_VCENTER);
    };
    const int height=r.bottom-r.top;
    const int pad=std::max(7,height/6);
    bool active=(item.itemState&ODS_SELECTED)!=0;
    surface.paint(dc,item.hwndItem,window,r);
    if(item.CtlID==PURPOSE_SCROLL || item.CtlID==RESULT_SCROLL) {
        (item.CtlID==RESULT_SCROLL?results_scroll:purposes_scroll).draw(dc);
    } else if(item.CtlID==SEARCH_ICON) {
        int size=height/2;
        icon(dc,SEARCH_ICON,(r.left+r.right-size)/2,(r.top+r.bottom-size)/2,size,muted);
    } else if(item.CtlID==EMPTY_RESULTS) {
        RECT mark{r.left+(r.right-r.left)/2-16,r.top+8,r.left+(r.right-r.left)/2+16,r.top+40};
        icon(dc,SEARCH_ICON,mark.left,mark.top,32,subdued);
        RECT title=r;title.top+=48;title.bottom=title.top+30;
        text(entries.empty()?L"让你的插件各就其位":L"没有匹配的插件",title,ink,bold,DT_CENTER);
        RECT hint=r;hint.top=title.bottom+4;hint.bottom=hint.top+28;
        text(entries.empty()?L"选择插件目录，然后扫描插件库":L"试试其他关键词、厂商或用途",hint,muted,nullptr,DT_CENTER);
    } else if(item.CtlID==PREVIEW) {
        rounded(dc,r,6,preview_canvas,divider);
        InflateRect(&r,-5,-5);
        if(!image.pixels.empty()) {
            if(!previous_image.pixels.empty()) previous_image.draw(dc,r);
            image.draw(dc,r,BYTE(std::lround(255*preview_blend.value())));
        } else {
            const int w=r.right-r.left,h=r.bottom-r.top;
            // A quiet signal print identifies a missing thumbnail without imitating a product UI.
            for(int i=1;i<6;++i) {
                int y=r.top+i*h/6;
                fill(dc,{r.left+14,y,r.right-14,y+1},RGB(29,33,35));
            }
            for(int i=1;i<10;++i) {
                int x=r.left+i*w/10;
                fill(dc,{x,r.top+12,x+1,r.bottom-12},RGB(29,33,35));
            }
            if(preview_index) {
                const auto& entry=entries.at(*preview_index);
                uint32_t seed=2166136261U;
                for(unsigned char c:entry.plugin_key) seed=(seed^c)*16777619U;
                auto color=entry.kind=="Generators"?instrument:signal;
                for(int i=0;i<39;++i) {
                    double wave=std::abs(std::sin(i*(.09+(seed%19)*.009)+seed%17)*std::cos(i*.065));
                    int amplitude=3+int(wave*h*.24), x=r.left+(i+3)*w/45, mid=r.top+h/2;
                    fill(dc,{x,mid-amplitude,x+std::max(2,w/140),mid+amplitude},motion::mix(edge,color,.35+.65*wave));
                }
                RECT caption{r.left+12,r.top+8,r.right-12,r.top+30};
                text(L"ATLAS  /  SIGNAL",caption,subdued,mono);
                caption={r.left+12,r.bottom-28,r.right-12,r.bottom-8};
                text(L"暂无产品截图",caption,subdued,nullptr,DT_RIGHT);
            } else {
                RECT mark{r.left+w/2-18,r.top+h/2-18,r.left+w/2+18,r.top+h/2+18};
                signal_mark(dc,mark,edge,false,preview_canvas);
            }
        }
    } else if(item.CtlID==PREVIEW_TAGS) {
        const auto caption=field(window,PREVIEW_TAGS);
        if(!caption.empty()) {
            RECT labelbox=r;labelbox.right=labelbox.left+42;
            text(L"用途",labelbox,muted);
            r.left+=44;
            text(caption,r,selected_text);
        }
    } else if(item.CtlType==ODT_STATIC) {
        std::wstring caption(GetWindowTextLengthW(item.hwndItem)+1,0);
        caption.resize(GetWindowTextW(item.hwndItem,caption.data(),int(caption.size())));
        auto font=reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0));
        if(item.CtlID==TITLE) {
            int side=height*3/4;
            RECT mark{r.left,r.top+(height-side)/2,r.left+side,r.top+(height+side)/2};
            rounded(dc,mark,5,signal,signal);
            RECT bars=mark;InflateRect(&bars,-side/4,-side/4);
            signal_mark(dc,bars,on_signal,false,signal);
            r.left+=side+14;
            SelectObject(dc,font);SIZE size{};GetTextExtentPoint32W(dc,caption.c_str(),int(caption.size()),&size);
            RECT name=r;name.right=std::min(r.right,r.left+size.cx+2);
            text(caption,name,ink,font);
            RECT edition{name.right+15,r.top+height/3,name.right+76,r.top+height*3/4};
            text(L"STUDIO",edition,signal,mono);
        } else if(item.CtlID==RESULT_COUNT) {
            text(caption,r,subdued,nullptr,DT_RIGHT);
        } else if(item.CtlID==PREVIEW_SOURCE) {
            text(caption,r,caption==L"识别封面"?subdued:instrument,nullptr,DT_RIGHT);
        } else if(item.CtlID==STATUS) {
            if(caption.empty()) caption=L"就绪  ·  浏览与分类保存在本机";
            int dot=4,cy=(r.top+r.bottom)/2;
            rounded(dc,{r.left,cy-dot/2,r.left+dot,cy+dot/2},2,instrument,instrument);
            r.left+=12;text(caption,r,subdued,font);
        } else if(item.CtlID==FOOTNOTE) {
            text(caption,r,subdued,font,DT_RIGHT);
        } else {
            bool strong=item.CtlID==PREVIEW_NAME || item.CtlID==LIST_HEADING;
            text(caption,r,strong?ink:muted,font);
        }
    } else if(item.CtlType==ODT_LISTBOX && item.itemID!=UINT(-1)) {
        auto& scroll=item.CtlID==RESULTS?results_scroll:purposes_scroll;
        const auto state=scroll.appearance(int(item.itemID),active);
        RECT row=r;InflateRect(&row,-2,-2);
        const COLORREF floor=item.CtlID==PURPOSES?sidebar:background;
        auto tint=motion::mix(floor,face,state.hover);
        tint=motion::mix(tint,selection,state.selection);
        auto rim=motion::mix(tint,selection_edge,state.selection*.65);
        rounded(dc,row,5,tint,rim);
        if(state.selection>.01) {
            int inset=std::max(7,height/4);
            fill(dc,{row.left,row.top+inset,row.left+3,row.bottom-inset},motion::mix(tint,signal,state.selection));
        }
        r.left+=pad+5;r.right-=pad;
        if(item.CtlID==RESULTS && item.itemID<visible.size()) {
            const auto& e=entries[visible[item.itemID]];
            bool keys=e.kind=="Generators";
            const int side=std::min(34,height-18),cy=(r.top+r.bottom)/2;
            RECT mark{r.left,cy-side/2,r.left+side,cy+(side+1)/2};
            auto tileColor=keys?instrument_surface:input_surface;
            rounded(dc,mark,5,tileColor,tileColor);
            InflateRect(&mark,-side/4,-side/4);
            signal_mark(dc,mark,keys?instrument:muted,keys,tileColor);
            r.left+=side+12;
            RECT format=r;format.left=format.right-42;format.bottom=cy+2;
            text(wide(e.format),format,active?selected_text:subdued,mono,DT_RIGHT);
            RECT title=r;title.bottom=cy+2;title.right-=46;
            text(wide(e.name),title,ink,bold);
            std::wstring detail=wide(e.vendor.empty()?flcat::unknown_vendor:e.vendor);
            detail+=L"  ·  ";
            if(e.tags.empty()) detail+=wide(flcat::unclassified);
            for(const auto& tag:e.tags) { if(detail.back()!=L' ') detail+=L" / "; detail+=wide(tag.first); }
            RECT subtitle=r;subtitle.top=cy+2;subtitle.bottom-=4;
            text(detail,subtitle,active?selected_text:muted);
        } else if(item.CtlID==PURPOSES) {
            auto caption=label(item.hwndItem,item.itemID);
            auto key=item.itemID==0?std::string{}:utf8(caption);
            size_t count=purpose_counts.count(key)?purpose_counts.at(key):0;
            int side=std::min(14,height/2);
            RECT mark{r.left,(r.top+r.bottom-side)/2,r.left+side,(r.top+r.bottom+side)/2};
            if(item.itemID==0) {
                int half=side/2-2;
                for(int x=0;x<2;++x) for(int y=0;y<2;++y)
                    rounded(dc,{mark.left+x*(half+3),mark.top+y*(half+3),mark.left+x*(half+3)+half,mark.top+y*(half+3)+half},1,active?signal:subdued,active?signal:subdued);
            } else signal_mark(dc,mark,active?signal:subdued,caption==L"合成器" || caption==L"采样器",tint);
            r.left+=side+11;
            RECT number=r;number.left=r.right-30;
            text(std::to_wstring(count),number,active?signal:subdued,mono,DT_RIGHT);
            r.right=number.left-3;
            text(caption,r,count || active?ink:subdued,active?bold:nullptr);
        }
    } else if(item.CtlType==ODT_BUTTON) {
        bool tab=item.CtlID>=TYPE_ALL && item.CtlID<=TYPE_INSTRUMENTS;
        bool current=tab && item.CtlID-TYPE_ALL==type_filter;
        bool primary=item.CtlID==ADD_PLUGIN || (item.CtlID==SCAN && entries.empty());
        bool disabled=item.itemState&ODS_DISABLED;
        bool quiet=item.CtlID==IMAGE_RESET || tab;
        auto animation=motion::Button::get(item.hwndItem);
        double hot=animation?animation->hover.value():0,pushed=animation?animation->press.value():(active?1.:0.);
        auto floor=item.CtlID==EDIT_MENU?footer:item.CtlID==IMAGE_PICK || item.CtlID==IMAGE_RESET || item.CtlID==ADD_PLUGIN?inspector:background;
        auto body=motion::mix(quiet?floor:face,hover,hot);
        auto rim=motion::mix(quiet?floor:edge,muted,hot*.35);
        auto fg=ink;
        if(primary) {body=motion::mix(signal,focus,hot);rim=body;fg=on_signal;}
        if(current) {body=selection;rim=selection;fg=signal;}
        body=motion::mix(body,disabled?face:floor,pushed*.28);
        if(disabled) {body=quiet?floor:input_surface;rim=quiet?floor:divider;fg=subdued;}
        rounded(dc,r,5,body,rim);
        if(current) fill(dc,{r.left+12,r.bottom-3,r.right-12,r.bottom-1},signal);
        auto caption=field(window,item.CtlID);
        HFONT font=primary || current?bold:base;
        if(!quiet && item.CtlID!=EDIT_MENU) {
            SelectObject(dc,font);SIZE size{};GetTextExtentPoint32W(dc,caption.data(),int(caption.size()),&size);
            int glyph=std::max(12L,size.cy-2),gap=7;
            int x=(r.left+r.right-size.cx-glyph-gap)/2;
            icon(dc,item.CtlID,x,(r.top+r.bottom-glyph)/2,glyph,fg);
            r.left=x+glyph+gap;text(caption,r,fg,font);
        } else text(caption,r,fg,font,DT_CENTER);
    }
    if((item.itemState&ODS_FOCUS) && !(item.itemState&ODS_NOFOCUSRECT)) {
        RECT ring=item.rcItem;InflateRect(&ring,-3,-3);
        SetTextColor(dc,focus);SetBkColor(dc,background);DrawFocusRect(dc,&ring);
    }
    RestoreDC(dc,saved);
    if(buffer) EndBufferedPaint(buffer,TRUE);
}
