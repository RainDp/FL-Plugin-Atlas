#include "panel.hpp"
#include "ids.h"
#include "palette.hpp"
#include "layout.hpp"
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
#include <gdiplus.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace fs = std::filesystem;
namespace {
using namespace palette;
HBRUSH color_brush(HDC dc,COLORREF color) {
    SetDCBrushColor(dc,color);
    return static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
}
void fill(HDC dc,RECT rect,COLORREF color) { FillRect(dc,&rect,color_brush(dc,color)); }
Gdiplus::Color tint(COLORREF color,BYTE opacity=255) {
    return {opacity,GetRValue(color),GetGValue(color),GetBValue(color)};
}
void round_path(Gdiplus::GraphicsPath& path,RECT r,int radius) {
    const float x=float(r.left)+.5f,y=float(r.top)+.5f;
    const float w=float(r.right-r.left)-1,h=float(r.bottom-r.top)-1;
    const float d=std::max(1.f,std::min(float(2*radius),std::min(w,h)));
    path.AddArc(x,y,d,d,180.f,90.f);
    path.AddArc(x+w-d,y,d,d,270.f,90.f);
    path.AddArc(x+w-d,y+h-d,d,d,0.f,90.f);
    path.AddArc(x,y+h-d,d,d,90.f,90.f);
    path.CloseFigure();
}
void rounded(HDC dc,RECT r,int radius,COLORREF body,COLORREF outline) {
    if(r.right-r.left<2 || r.bottom-r.top<2) return;
    Gdiplus::Graphics graphics(dc);
    if(graphics.GetLastStatus()==Gdiplus::Ok) {
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;round_path(path,r,radius);
        Gdiplus::SolidBrush brush(tint(body));
        Gdiplus::Pen pen(tint(outline),1.f);
        graphics.FillPath(&brush,&path);
        graphics.DrawPath(&pen,&path);
        return;
    }
    int saved=SaveDC(dc);
    SelectObject(dc,GetStockObject(DC_BRUSH)); SetDCBrushColor(dc,body);
    SelectObject(dc,GetStockObject(DC_PEN)); SetDCPenColor(dc,outline);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,2*radius,2*radius);
    RestoreDC(dc,saved);
}
void glass_button(HDC dc,RECT r,int radius,COLORREF top,COLORREF bottom,COLORREF rim,BYTE opacity,
                  double reflection=.5,double strength=0) {
    const int w=r.right-r.left,h=r.bottom-r.top;
    if(w<2 || h<2) return;
    Gdiplus::Graphics graphics(dc);
    if(graphics.GetLastStatus()!=Gdiplus::Ok) {
        rounded(dc,r,radius,bottom,rim);
        return;
    }
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::GraphicsPath path;round_path(path,r,radius);
    Gdiplus::RectF box(float(r.left),float(r.top),float(w),float(h));
    Gdiplus::LinearGradientBrush fillBrush(box,tint(top,opacity),tint(bottom,opacity),Gdiplus::LinearGradientModeVertical);
    graphics.FillPath(&fillBrush,&path);
    Gdiplus::LinearGradientBrush edgeBrush(box,tint(rim,opacity),tint(rim,BYTE(opacity*.45)),Gdiplus::LinearGradientModeVertical);
    Gdiplus::Pen pen(&edgeBrush,1.f);
    graphics.DrawPath(&pen,&path);
    if(strength>0) {
        auto saved=graphics.Save();
        graphics.SetClip(&path,Gdiplus::CombineModeIntersect);
        float size=float(w)*.60f,cx=float(r.left+w*reflection);
        Gdiplus::GraphicsPath glowPath;
        glowPath.AddEllipse(cx-size*.5f,float(r.top)-h*.32f,size,float(h)*1.05f);
        Gdiplus::PathGradientBrush glow(&glowPath);
        glow.SetCenterColor(tint(ink,BYTE(std::lround(28*strength))));
        Gdiplus::Color transparent=tint(ink,0);
        int count=1;glow.SetSurroundColors(&transparent,&count);
        graphics.FillPath(&glow,&glowPath);
        graphics.Restore(saved);
    }
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
    Gdiplus::Graphics graphics(dc);
    if(graphics.GetLastStatus()!=Gdiplus::Ok || size<2) return;
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.TranslateTransform(float(x),float(y));
    graphics.ScaleTransform(float(size)/16.f,float(size)/16.f);
    Gdiplus::Pen pen(tint(color),1.6f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    auto line=[&](float a,float b,float c,float d){graphics.DrawLine(&pen,a,b,c,d);};
    if(id==SEARCH_ICON) {
        graphics.DrawEllipse(&pen,1.5f,1.5f,9.f,9.f); line(10,10,14.5f,14.5f);
    } else if(id==BROWSE) {
        Gdiplus::PointF folder[]={{1,4},{6,4},{8,6},{15,6},{15,13},{1,13},{1,4}};
        graphics.DrawLines(&pen,folder,7);
    } else if(id==FILL_IMAGES) {
        line(8,1,8,11); line(4,7,8,11); line(8,11,12,7);
        line(1,11,1,15); line(1,14,15,14); line(15,15,15,11);
    } else if(id==IMAGE_PICK) {
        graphics.DrawRectangle(&pen,1.5f,2.5f,13.f,11.f);
        line(2.5f,11.5f,6,8); line(6,8,9,11); line(9,11,13.5f,6.5f);
    } else if(id==ADD_PLUGIN) {
        line(8,2.5f,8,13.5f); line(2.5f,8,13.5f,8);
    } else if(id==SCAN) {
        for(float a:{2.f,10.f}) {line(a,2,a+4,2);line(a,14,a+4,14);}
        for(float a:{2.f,14.f}) {line(a,2,a,6);line(a,10,a,14);}
        line(4,8,12,8);
    }
}

LRESULT CALLBACK edit_hint(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    auto result=DefSubclassProc(w,msg,wp,lp);
    if(msg==WM_PAINT || msg==WM_PRINTCLIENT || msg==WM_PRINT) {
        HDC dc=msg==WM_PAINT?GetDC(w):reinterpret_cast<HDC>(wp);
        if(dc) {
            auto saved=SaveDC(dc);
            RECT box{}; GetClientRect(w,&box);
            SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(w,WM_GETFONT,0,0)));
            SetTextColor(dc,muted);SetBkMode(dc,TRANSPARENT);
            if(!GetWindowTextLengthW(w) && GetFocus()!=w)
                DrawTextW(dc,GetDlgCtrlID(w)==VALUE?L"输入用途或厂商名":L"搜索插件或厂商",-1,&box,
                          DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
            if(GetFocus()==w) fill(dc,{box.left,box.bottom-2,box.right,box.bottom},signal);
            RestoreDC(dc,saved);
            if(msg==WM_PAINT) ReleaseDC(w,dc);
        }
    }
    if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS) InvalidateRect(w,nullptr,TRUE);
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

void purpose_mark(HDC dc,RECT box,const std::wstring& label,COLORREF color) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.TranslateTransform(float(box.left),float(box.top));
    graphics.ScaleTransform(float(box.right-box.left)/16,float(box.bottom-box.top)/16);
    graphics.SetClip(Gdiplus::RectF(0,0,16,16));
    Gdiplus::Pen pen(tint(color),1.2f);
    pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
    auto line=[&](float x,float y,float a,float b){graphics.DrawLine(&pen,x,y,a,b);};
    if(label==L"全部") {
        for(int y:{2,9}) for(int x:{2,9}) graphics.DrawRectangle(&pen,x,y,5,5);
    } else if(label==L"均衡" || label==L"增益" || label==L"动态处理" || label==L"母带") {
        for(int i=0;i<3;++i) {
            float x=3.f+i*5,y=4.f+(i%2)*6;
            line(x,2,x,y-2);line(x,y+2,x,14);
            line(x-2,y,x+2,y);
        }
    } else if(label==L"合成器" || label==L"采样器") {
        graphics.DrawRectangle(&pen,1,3,14,10);
        for(float x:{5.f,10.f}) line(x,3,x,13);
        line(3,3,3,8);line(8,3,8,8);line(13,3,13,8);
    } else if(label==L"混响") {
        for(int i=0;i<3;++i) graphics.DrawArc(&pen,1.f-i*3,1.f-i*3,10.f+i*6,14.f+i*6,-65,130);
    } else if(label==L"延迟") {
        graphics.DrawEllipse(&pen,1.5f,1.5f,13.f,13.f);
        line(8,4,8,8);line(8,8,11,10);
    } else if(label==L"人声") {
        graphics.DrawEllipse(&pen,5,1,6,9);
        graphics.DrawArc(&pen,3,3,10,10,0,180);
        line(8,13,8,15);line(5,15,11,15);
    } else if(label==L"压缩") {
        line(1,8,6,8);line(3,5,6,8);line(3,11,6,8);
        line(15,8,10,8);line(13,5,10,8);line(13,11,10,8);
        line(8,2,8,14);
    } else if(label==L"失真") {
        Gdiplus::PointF points[]={{9,1},{3,9},{8,9},{7,15},{14,6},{9,6},{9,1}};
        graphics.DrawLines(&pen,points,7);
    } else {
        Gdiplus::PointF points[]={{1,9},{4,9},{6,3},{9,13},{11,7},{15,7}};
        graphics.DrawLines(&pen,points,6);
    }
}

LRESULT CALLBACK vendor_face(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    if(msg!=WM_PAINT && msg!=WM_PRINTCLIENT && msg!=WM_PRINT) return DefSubclassProc(w,msg,wp,lp);
    PAINTSTRUCT ps{};
    HDC dc=msg==WM_PAINT?BeginPaint(w,&ps):reinterpret_cast<HDC>(wp);
    if(dc) {
        int saved=SaveDC(dc);
        RECT r{}; GetClientRect(w,&r);
        reinterpret_cast<GlassSurface*>(data)->paint(dc,w,GetParent(w),r);
        int h=r.bottom;
        glass_button(dc,r,8,hover,input_surface,GetFocus()==w?focus:edge,185);
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
    DeleteObject(body); body=nullptr;
    DeleteObject(section); section=nullptr;
    DeleteObject(small); small=nullptr;
    preview_index.reset(); image={}; previous_image={}; preview_blend.reset(1);
    glass.clear();
    if(buffered_paint) BufferedPaintUnInit();
    buffered_paint=false;
    if(graphics_token) Gdiplus::GdiplusShutdown(graphics_token);
    graphics_token=0;
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
            return reinterpret_cast<INT_PTR>(panel->glass.brush_for(dc,reinterpret_cast<HWND>(lp),w));
        }
        if(msg==WM_DRAWITEM) {panel->draw(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;}
        if (msg==WM_NCDESTROY) panel->window=nullptr;
        if (msg==WM_TIMER && wp==1) { panel->complete(); return TRUE; }
        if (msg==WM_TIMER && wp==2) { panel->finish_images(); return TRUE; }
        if (msg==WM_TIMER && wp==3) { panel->animate_preview(); return TRUE; }
        if(msg==WM_SETTINGCHANGE) {
            for(int id:{BROWSE,FILL_IMAGES,SCAN,ADD_PLUGIN,IMAGE_PICK,IMAGE_RESET,EDIT_MENU})
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
        else if(id==RESULTS && HIWORD(wp)==LBN_SELCHANGE) {
            auto list=GetDlgItem(w,RESULTS);
            auto sel=selected(list);
            int focus=static_cast<int>(SendMessageW(list,LB_GETCARETINDEX,0,0));
            if(!sel.empty() && SendMessageW(list,LB_GETSEL,focus,0)<=0) focus=sel.front();
            panel->preview(sel.empty()?std::nullopt:std::optional<size_t>(panel->visible.at(focus)));
            if(!panel->job.valid() && !panel->image_job.valid())
                status(w,L"已选择 "+std::to_wstring(sel.size())+L" 个预设");
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
    Gdiplus::GdiplusStartupInput graphics;
    Gdiplus::GdiplusStartup(&graphics_token,&graphics,nullptr);
    RECT editor{};GetClientRect(window,&editor);
    SetWindowPos(window,nullptr,0,0,layout::scale(editor.right),layout::scale(editor.bottom),
                 SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
        RECT box{};GetWindowRect(child,&box);
        if(GetDlgCtrlID(child)==VENDORS)
            SendMessageW(child,CB_GETDROPPEDCONTROLRECT,0,reinterpret_cast<LPARAM>(&box));
        MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&box),2);
        layout::scale_rect(box);
        SetWindowPos(child,nullptr,box.left,box.top,box.right-box.left,box.bottom-box.top,
                     SWP_NOZORDER|SWP_NOACTIVATE);
    }
    glass.build(window);
    LOGFONTW font{};
    GetObjectW(reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)),sizeof(font),&font);
    font.lfQuality=CLEARTYPE_QUALITY;
    int baseHeight=font.lfHeight;
    auto font_height=[&](int numerator,int denominator,int minimum) {
        return -std::max(minimum,std::abs(layout::scale(MulDiv(baseHeight,numerator,denominator))));
    };
    font.lfHeight=font_height(1,1,11);
    small=CreateFontIndirectW(&font);
    for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(small),FALSE);
    font.lfHeight=font_height(7,6,12);
    body=CreateFontIndirectW(&font);
    font.lfWeight=FW_SEMIBOLD;
    bold=CreateFontIndirectW(&font);
    font.lfHeight=font_height(16,10,14);
    section=CreateFontIndirectW(&font);
    for(int id:{LIST_HEADING,PREVIEW_HEADING})
        SendDlgItemMessageW(window,id,WM_SETFONT,reinterpret_cast<WPARAM>(section),FALSE);
    font.lfHeight=font_height(20,10,18);
    display=CreateFontIndirectW(&font);
    SendDlgItemMessageW(window,PREVIEW_NAME,WM_SETFONT,reinterpret_cast<WPARAM>(display),FALSE);
    wcscpy_s(font.lfFaceName,L"Segoe UI");
    font.lfHeight=font_height(17,10,16);
    heading=CreateFontIndirectW(&font);
    SendDlgItemMessageW(window,TITLE,WM_SETFONT,reinterpret_cast<WPARAM>(heading),FALSE);
    for(int id:{VENDOR_HEADING,PURPOSE_HEADING})
        SendDlgItemMessageW(window,id,WM_SETFONT,reinterpret_cast<WPARAM>(bold),FALSE);
    SendDlgItemMessageW(window,PREVIEW_PATH,WM_SETFONT,reinterpret_cast<WPARAM>(small),FALSE);
    for(int id:{VENDORS,SEARCH_PLUGINS,VALUE,PREVIEW_META})
        SendDlgItemMessageW(window,id,WM_SETFONT,reinterpret_cast<WPARAM>(body),FALSE);
    RECT units{0,0,1,24}; MapDialogRect(window,&units);
    layout::scale_rect(units);
    // Fit whole rows in both lists at the host's actual dialog scale.
    auto fit_rows=[&](int list,int rail,int preferred) {
        RECT area{};GetClientRect(GetDlgItem(window,list),&area);
        int rows=std::max(1L,area.bottom/preferred),rowHeight=area.bottom/rows;
        SendDlgItemMessageW(window,list,LB_SETITEMHEIGHT,0,rowHeight);
        for(int id:{list,rail}) {
            auto child=GetDlgItem(window,id);RECT box{};GetClientRect(child,&box);
            SetWindowPos(child,nullptr,0,0,box.right,rows*rowHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
    };
    fit_rows(RESULTS,RESULT_SCROLL,units.bottom);
    fit_rows(PURPOSES,PURPOSE_SCROLL,units.bottom*2/3);
    SendDlgItemMessageW(window,VENDORS,CB_SETITEMHEIGHT,-1,units.bottom/2-2);
    SetWindowSubclass(GetDlgItem(window,VENDORS),vendor_face,1,reinterpret_cast<DWORD_PTR>(&glass));
    for(int id:{PURPOSES,RESULTS,VENDORS}) SetWindowTheme(GetDlgItem(window,id),L"DarkMode_Explorer",nullptr);
    size_t button=0;
    for(int id:{BROWSE,FILL_IMAGES,SCAN,ADD_PLUGIN,IMAGE_PICK,IMAGE_RESET,EDIT_MENU})
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
    if(SendDlgItemMessageW(window,VENDORS,CB_GETCURSEL,0,0)>0) f.vendor=utf8(field(window,VENDORS));
    HWND purposes=GetDlgItem(window,PURPOSES);
    int purpose=static_cast<int>(SendMessageW(purposes,LB_GETCURSEL,0,0));
    if(purpose>0) f.tags.push_back(utf8(label(purposes,purpose)));
    auto list=GetDlgItem(window,RESULTS);
    SendMessageW(list,WM_SETREDRAW,FALSE,0);
    SendMessageW(list,LB_RESETCONTENT,0,0);
    visible.clear();
    for(size_t i=0;i<entries.size();++i) {
        const auto& e=entries[i];
        if(!f.matches(e)) continue;
        auto name=wide(e.name), vendor=wide(e.vendor.empty()?flcat::unknown_vendor:e.vendor);
        auto contains=[&](const std::wstring& s) {
            return !s.empty() && FindNLSStringEx(LOCALE_NAME_INVARIANT,FIND_FROMSTART|NORM_IGNORECASE,
                s.data(),int(s.size()),query.data(),int(query.size()),nullptr,nullptr,nullptr,0)>=0;
        };
        if(!query.empty() && !contains(name) && !contains(vendor)) continue;
        auto text=name+L"   ·   "+vendor+L"   ·   ";
        if(e.tags.empty()) text+=wide(flcat::unclassified);
        for(auto it=e.tags.begin();it!=e.tags.end();++it) { if(it!=e.tags.begin()) text+=L"、"; text+=wide(it->first); }
        if(SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()))>=0) visible.push_back(i);
    }
    if(!visible.empty()) SendMessageW(list,LB_SETSEL,TRUE,0);
    preview(visible.empty()?std::nullopt:std::optional<size_t>(visible.front()));
    SendMessageW(list,WM_SETREDRAW,TRUE,0);
    InvalidateRect(list,nullptr,TRUE);
    ShowWindow(GetDlgItem(window,EMPTY_RESULTS),visible.empty()?SW_SHOWNOACTIVATE:SW_HIDE);
    SetDlgItemTextW(window,RESULT_COUNT,(std::to_wstring(visible.size())+L" 个预设入口").c_str());
    if(!job.valid() && !image_job.valid()) status(window,visible.empty()?L"":L"已选择 1 个预设");
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
        meta=wide(e->vendor.empty()?flcat::unknown_vendor:e->vendor)+L" / "+
             (e->kind=="Effects"?L"效果器":L"乐器");
        if(!e->format.empty()) meta+=L" / "+wide(e->format);
        path=wide(e->path);
    }
    SetDlgItemTextW(window,PREVIEW_NAME,e?wide(e->name).c_str():L"未选择插件");
    if(!e) meta=entries.empty()?L"扫描插件后可查看预览":visible.empty()?L"试试其他关键词、厂商或用途":L"在列表中选择插件以查看预览";
    SetDlgItemTextW(window,PREVIEW_META,meta.c_str());
    SetDlgItemTextW(window,PREVIEW_PATH,path.c_str());
    SetDlgItemTextW(window,PREVIEW_SOURCE,source.c_str());
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
    auto text=[&](const std::wstring& value,RECT box,COLORREF color,HFONT font,UINT flags) {
        SelectObject(dc,font?font:small);
        SetTextColor(dc,color);
        auto ellipsis=item.CtlID==PREVIEW_PATH?DT_PATH_ELLIPSIS:DT_END_ELLIPSIS;
        DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&box,flags|DT_NOPREFIX|ellipsis|DT_SINGLELINE|DT_VCENTER);
    };
    const int pad=std::max(6L,(r.bottom-r.top)/5);
    bool active=(item.itemState&ODS_SELECTED)!=0;
    glass.paint(dc,item.hwndItem,window,r);
    if(item.CtlID==PURPOSE_SCROLL || item.CtlID==RESULT_SCROLL) {
        (item.CtlID==RESULT_SCROLL?results_scroll:purposes_scroll).draw(dc);
    } else if(item.CtlID==SEARCH_ICON) {
        int size=(r.bottom-r.top)/2;
        icon(dc,SEARCH_ICON,(r.left+r.right-size)/2,(r.top+r.bottom-size)/2,size,muted);
    } else if(item.CtlID==PREVIEW) {
        int radius=std::max(12,int((r.right-r.left)/22));
        bool pictured=!image.pixels.empty();
        auto well=pictured?preview_canvas:RGB(28,29,38);
        rounded(dc,r,radius,well,motion::mix(edge,signal,pictured?.28:.16));
        RECT stage=r; InflateRect(&stage,-7,-7);
        int stage_radius=std::max(8,radius-6);
        if(!image.pixels.empty()) {
            HRGN clip=CreateRoundRectRgn(stage.left,stage.top,stage.right+1,stage.bottom+1,stage_radius*2,stage_radius*2);
            SelectClipRgn(dc,clip);
            if(!previous_image.pixels.empty()) previous_image.draw(dc,stage);
            image.draw(dc,stage,BYTE(std::lround(255*preview_blend.value())));
            SelectClipRgn(dc,nullptr);
            DeleteObject(clip);
        } else if(preview_index) {
            const auto& entry=entries.at(*preview_index);
            uint32_t seed=2166136261U;
            for(unsigned char c:entry.plugin_key) seed=(seed^c)*16777619U;
            int w=stage.right-stage.left,h=stage.bottom-stage.top;
            int mid=stage.top+h/2;
            int baseline=std::max(1,h/180);
            fill(dc,{stage.left+w/12,mid,stage.right-w/12,mid+baseline},motion::mix(well,edge,.9));
            // A deterministic signal print distinguishes entries without a screenshot.
            for(int i=0;i<27;++i) {
                double wave=std::abs(std::sin(i*(0.11+(seed%23)*0.015)+(seed%17))*
                                     std::cos(i*0.074+((seed>>8)%11)));
                int height=2+int(wave*h*.22);
                int x=stage.left+(i+2)*w/31;
                int bar=std::max(2,w/140);
                fill(dc,{x,mid-height,x+bar,mid+height},motion::mix(subdued,signal,.25+wave*.7));
            }
            RECT cap{stage.left+12,stage.bottom-28,stage.right-12,stage.bottom-8};
            text(L"识别封面",cap,muted,small,DT_RIGHT);
        } else {
            int mw=std::max(36L,(r.right-r.left)/7);
            int mh=mw*2/3;
            int cx=(r.left+r.right)/2, cy=(r.top+r.bottom)/2;
            signal_mark(dc,{cx-mw/2,cy-mh/2,cx+mw/2,cy+mh/2},muted,false,well);
        }
    } else if(item.CtlID==EMPTY_RESULTS) {
        int mid=(r.left+r.right)/2;
        icon(dc,SEARCH_ICON,mid-12,r.top+12,24,subdued);
        RECT title{r.left,r.top+52,r.right,r.top+82};
        RECT hint{r.left,r.top+88,r.right,r.top+116};
        text(entries.empty()?L"插件库尚未载入":L"没有匹配的插件",title,ink,body,DT_CENTER);
        text(entries.empty()?L"选择目录或扫描插件":L"调整搜索词或筛选条件",hint,muted,nullptr,DT_CENTER);
    } else if(item.CtlType==ODT_STATIC) {
        std::wstring caption(GetWindowTextLengthW(item.hwndItem)+1,0);
        caption.resize(GetWindowTextW(item.hwndItem,caption.data(),static_cast<int>(caption.size())));
        auto font=reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0));
        if(item.CtlID==TITLE) {
            auto h=r.bottom-r.top;
            RECT tile{r.left,r.top+h/8,r.left+h*3/4,r.bottom-h/8};
            glass_button(dc,tile,h/4,focus,signal,focus,255);
            InflateRect(&tile,-h/6,-h/5); signal_mark(dc,tile,on_signal,false,signal);
            r.left+=h+h/8;
        }
        if(item.CtlID==PREVIEW_SOURCE && !caption.empty()) {
            int dot=std::max(4L,(r.bottom-r.top)/6);
            int y=(r.top+r.bottom-dot)/2;
            auto light=caption==L"识别封面"?subdued:signal;
            rounded(dc,{r.left,y,r.left+dot,y+dot},dot/2,light,light);
            r.left+=dot*2;
        }
        bool strong=item.CtlID==TITLE || item.CtlID==PREVIEW_NAME || item.CtlID==LIST_HEADING || item.CtlID==PREVIEW_HEADING;
        text(caption,r,strong?ink:muted,font,DT_LEFT);
    } else if(item.CtlType==ODT_LISTBOX) {
        if(item.itemID!=UINT(-1)) {
            auto& scroll=item.CtlID==RESULTS?results_scroll:purposes_scroll;
            auto appearance=scroll.appearance(int(item.itemID),active);
            double strength=appearance.selection;
            if(strength>0 || appearance.hover>0) {
                RECT chip=r; InflateRect(&chip,-3,-2);
                auto opacity=BYTE(std::lround(228*strength+78*appearance.hover*(1-strength)));
                glass_button(dc,chip,7,motion::mix(RGB(46,44,58),RGB(118,104,156),strength),
                             motion::mix(face,selection,strength),motion::mix(edge,selection_edge,strength),opacity);
                if(strength>0.2) {
                    int inset=std::max(5L,(chip.bottom-chip.top)/4);
                    RECT bar{chip.left+5,chip.top+inset,chip.left+8,chip.bottom-inset};
                    auto light=motion::mix(selection_edge,signal,strength);
                    rounded(dc,bar,2,light,light);
                }
            }
            r.left+=pad+8+int(std::lround(2*strength)); r.right-=8;
            if(item.CtlID==RESULTS && item.itemID<visible.size()) {
                const auto& e=entries[visible[item.itemID]];
                int size=(r.bottom-r.top)*58/100;
                int y=(r.top+r.bottom-size)/2;
                RECT tile{r.left,y,r.left+size,y+size};
                bool instrument=e.kind=="Generators";
                auto tileColor=instrument?RGB(62,50,74):face;
                rounded(dc,tile,std::max(3,size/4),tileColor,motion::mix(tileColor,edge,.75));
                InflateRect(&tile,-size/4,-size/4);
                signal_mark(dc,tile,motion::mix(muted,signal,std::max(appearance.hover*.45,strength)),instrument,tileColor);
                r.left+=size+pad;
                bool known_vendor=!e.vendor.empty() && e.vendor!=flcat::unknown_vendor;
                RECT top=r, bottom=r;
                if(!e.tags.empty() || known_vendor) {
                    top.bottom=r.top+(r.bottom-r.top)*58/100;
                    bottom.top=top.bottom-1;
                }
                if(known_vendor) {
                    auto vendor=wide(e.vendor);
                    SelectObject(dc,small);
                    SIZE extent{}; GetTextExtentPoint32W(dc,vendor.data(),int(vendor.size()),&extent);
                    int width=std::min(int(extent.cx)+16,int(bottom.right-bottom.left));
                    if(width>=36) {
                        int mid=(bottom.top+bottom.bottom)/2;
                        RECT badge{bottom.right-width,mid-extent.cy/2-2,bottom.right,mid+extent.cy/2+2};
                        rounded(dc,badge,(badge.bottom-badge.top)/2,
                                motion::mix(input_surface,selection,strength*.45),
                                motion::mix(edge,selection_edge,strength));
                        RECT label=badge; InflateRect(&label,-7,0);
                        text(vendor,label,active?selected_text:muted,nullptr,DT_CENTER);
                        bottom.right=badge.left-8;
                    }
                }
                if(!e.tags.empty()) {
                    std::wstring tags;
                    for(const auto& tag:e.tags){if(!tags.empty()) tags+=L" / ";tags+=wide(tag.first);}
                    text(tags,bottom,active?selected_text:muted,nullptr,DT_LEFT);
                }
                text(wide(e.name),top,ink,bold,DT_LEFT);
            } else if(item.CtlID==PURPOSES) {
                auto caption=label(item.hwndItem,item.itemID);
                int size=(r.bottom-r.top)/2;
                int y=(r.top+r.bottom-size)/2;
                purpose_mark(dc,{r.left,y,r.left+size,y+size},caption,
                             motion::mix(muted,signal,appearance.selection));
                r.left+=size+12;
                text(caption,r,ink,active?bold:body,DT_LEFT);
            }
        }
    } else if(item.CtlType==ODT_BUTTON) {
        bool disabled=item.itemState&ODS_DISABLED;
        bool lead=!disabled && (item.CtlID==ADD_PLUGIN || (item.CtlID==SCAN && entries.empty()));
        auto animation=motion::Button::get(item.hwndItem);
        double hovered=animation?animation->hover.value():0,pressed=animation?animation->press.value():(active?1.:0.);
        double pointer=animation?animation->pointer.value():.5;
        bool quiet=item.CtlID==IMAGE_RESET;
        if(!disabled && (!quiet || pressed>0 || hovered>0)) {
            int inset=int(std::lround(pressed));
            InflateRect(&r,-inset,-inset);
            auto top=motion::mix(RGB(58,58,70),RGB(84,78,104),hovered);
            auto rim=motion::mix(edge,focus,hovered);
            if(lead)
                glass_button(dc,r,8,motion::mix(focus,RGB(255,252,255),hovered*.4),
                             motion::mix(signal,RGB(176,158,222),pressed),focus,252,pointer,
                             motion::enabled()?hovered*(1-pressed)*.7:0);
            else
                glass_button(dc,r,8,motion::mix(top,face,pressed),
                             motion::mix(input_surface,hover,pressed),rim,
                             BYTE(std::lround(quiet?185*std::max(hovered,pressed):200)),pointer,
                             motion::enabled()?hovered*(1-pressed):0);
        }
        auto caption=field(window,item.CtlID);
        auto fg=disabled?subdued:lead?on_signal:quiet?muted:ink;
        HFONT font=lead?bold:small;
        if(!quiet && item.CtlID!=EDIT_MENU) {
            SIZE size{};SelectObject(dc,font);GetTextExtentPoint32W(dc,caption.data(),int(caption.size()),&size);
            int glyph=std::max(12L,size.cy-2), gap=6;
            int x=(r.left+r.right-size.cx-glyph-gap)/2;
            icon(dc,item.CtlID,x,(r.top+r.bottom-glyph)/2,glyph,fg);
            r.left=x+glyph+gap; text(caption,r,fg,font,DT_LEFT);
        } else text(caption,r,fg,font,DT_CENTER);
    }
    if((item.itemState&ODS_FOCUS) && !(item.itemState&ODS_NOFOCUSRECT)){
        RECT ring=item.rcItem;InflateRect(&ring,-2,-2);
        Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;round_path(path,ring,7);
        Gdiplus::Pen pen(tint(focus,190),1.f);graphics.DrawPath(&pen,&path);
    }
    RestoreDC(dc,saved);
    if(buffer) EndBufferedPaint(buffer,TRUE);
}
