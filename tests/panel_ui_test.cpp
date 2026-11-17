#include <clap/clap.h>
#include <windows.h>
#include <commctrl.h>
#include "plugin_sandbox.hpp"
#include "../plugin/ids.h"
#include "flcat/store.hpp"
#include "flcat/paths.hpp"
#include <fstream>
#include <iostream>
#include <set>

namespace fs=std::filesystem;
static void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
static std::wstring text(HWND editor,int id) {
    auto w=GetDlgItem(editor,id);std::wstring result(GetWindowTextLengthW(w)+1,0);
    result.resize(GetWindowTextW(w,result.data(),int(result.size())));return result;
}
static void pump(HWND editor,unsigned duration) {
    auto end=GetTickCount64()+duration;
    do {
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            if(!IsDialogMessageW(editor,&msg)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        Sleep(1);
    } while(GetTickCount64()<end);
}
static void capture(HWND editor,const fs::path& path) {
    if(path.empty()) return;
    RECT r{};GetClientRect(editor,&r);
    auto screen=GetDC(editor),dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};auto& b=info.bmiHeader;
    b.biSize=sizeof(b);b.biWidth=r.right;b.biHeight=-r.bottom;b.biPlanes=1;b.biBitCount=32;
    void* pixels=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    check(bitmap!=nullptr,"capture buffer");auto old=SelectObject(dc,bitmap);
    SendMessageW(editor,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    GdiFlush();BITMAPFILEHEADER header{};
    header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(b);
    header.bfSize=header.bfOffBits+r.right*r.bottom*4;
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<char*>(&header),sizeof(header));out.write(reinterpret_cast<char*>(&b),sizeof(b));
    out.write(static_cast<char*>(pixels),r.right*r.bottom*4);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(editor,screen);
    check(bool(out),"capture output");
}
int main(int argc,char** argv) {
    try {
        auto default_plugin=(fs::absolute(argv[0]).parent_path()/L"FL Plugin Atlas.clap").string();
        const char* plugin_file=argc>1?argv[1]:default_plugin.c_str();
        bool preview=argc==1 || (argc>2 && std::string(argv[2])=="--preview");
        fs::path captures=argc>2 && !preview?fs::absolute(argv[2]):fs::path{};
        if(!captures.empty()) fs::create_directories(captures);
        plugin_sandbox::Temporary sandbox;
        auto library=sandbox.path/L"Documents/Image-Line/FL Studio/Presets/Plugin database";
        fs::create_directories(library);
        auto library_id=flcat::path_id(fs::weakly_canonical(library));
        auto database=sandbox.path/L"LocalAppData/FL Plugin Atlas/catalog.sqlite3";
        std::vector<flcat::Entry> rows={
            {"Installed/Effects/Pro-Q default.fst","proq","FabFilter Pro-Q 4","Effects","VST3","FabFilter",{{"均衡","scan"}}},
            {"Effects/Favorites/Pro-Q bright.fst","proq","FabFilter Pro-Q 4 (Bright vocal starting preset)","Effects","VST3","FabFilter",{{"均衡","scan"}}},
            {"Installed/Effects/Pro-L.fst","prol","FabFilter Pro-L 2","Effects","VST3","FabFilter",{{"母带","manual"}}},
            {"Installed/Effects/Acme EQ.fst","acme","Precision EQ","Effects","VST3","Acme",{{"均衡","scan"}}},
            {"Installed/Generators/Synth.fst","synth","Studio Synth","Generators","CLAP","Atlas Sample",{{"合成器","scan"}}},
            {"Installed/Unknown.fst","unknown","Unidentified preset","","VST","",{}}
        };
        for(int i=1;i<=12;++i) rows.push_back({"Effects/Processor "+std::to_string(i)+".fst","processor"+std::to_string(i),
            "Studio Processor "+std::to_string(i),"Effects","VST3","Atlas Sample",{{"压缩","scan"},{"人声","manual"}}});
        for(const auto& e:rows) {fs::create_directories((library/fs::u8path(e.path)).parent_path());std::ofstream(library/fs::u8path(e.path))<<e.path;}
        flcat::Store(database).replace_scan(library_id,rows);
        HMODULE module=LoadLibraryW(fs::absolute(plugin_file).c_str());check(module,"load plugin");
        plugin_sandbox::isolate(module,sandbox.path);
        auto entry=reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(module,"clap_entry"));
        check(entry && entry->init(plugin_file),"plugin entry");
        auto factory=static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        clap_host_t host{};host.clap_version=CLAP_VERSION;host.name="Atlas isolated UI verification";host.version="1";
        auto plugin=factory->create_plugin(factory,&host,factory->get_plugin_descriptor(factory,0)->id);
        check(plugin && plugin->init(plugin),"plugin instance");
        auto gui=static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin,CLAP_EXT_GUI));
        check(gui->create(plugin,CLAP_WINDOW_API_WIN32,false),"create editor");
        uint32_t width=0,height=0;check(gui->get_size(plugin,&width,&height),"editor size");
        HWND parent=CreateWindowExW(0,L"STATIC",L"FL Plugin Atlas · 隔离样本预览",
            preview?WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX:WS_POPUP,
            preview?80:-10000,preview?80:-10000,width+16,height+40,nullptr,nullptr,nullptr,nullptr);
        clap_window_t window{};window.api=CLAP_WINDOW_API_WIN32;window.win32=parent;
        check(gui->set_parent(plugin,&window) && gui->show(plugin),"embed editor");
        auto editor=GetWindow(parent,GW_CHILD);check(editor,"editor handle");
        ShowWindow(parent,SW_SHOWNOACTIVATE);pump(editor,250);
        if(preview) {
            SetFocus(GetDlgItem(editor,SEARCH_PLUGINS));
            while(IsWindow(parent)) pump(editor,25);
        } else {
            auto command=[&](int id,int code){SendMessageW(editor,WM_COMMAND,MAKEWPARAM(id,code),reinterpret_cast<LPARAM>(GetDlgItem(editor,id)));};
            auto select=[&](int id,const wchar_t* name) {
                auto combo=id==VENDORS;
                auto index=SendDlgItemMessageW(editor,id,combo?CB_FINDSTRINGEXACT:LB_FINDSTRINGEXACT,-1,reinterpret_cast<LPARAM>(name));
                check(index>=0,"filter option missing");
                SendDlgItemMessageW(editor,id,combo?CB_SETCURSEL:LB_SETCURSEL,index,0);
                command(id,combo?CBN_SELCHANGE:LBN_SELCHANGE);
            };
            auto count=[&]{return SendDlgItemMessageW(editor,RESULTS,LB_GETCOUNT,0,0);};
            auto enabled=[&](int id){return IsWindowEnabled(GetDlgItem(editor,id))!=FALSE;};
            auto shot=[&](const wchar_t* name){pump(editor,250);if(!captures.empty()) capture(editor,captures/name);};
            check(count()==18 && !enabled(FILTER_RESET) && !enabled(EDIT_MENU),"initial actions");
            shot(L"all.bmp");
            select(VENDORS,L"FabFilter");select(PURPOSES,L"均衡");
            check(count()==2 && text(editor,FILTER_SUMMARY)==L"FabFilter · 均衡","vendor and purpose intersection");
            SetDlgItemTextW(editor,SEARCH_PLUGINS,L"no matching product");
            check(count()==0 && !enabled(ADD_PLUGIN) && !enabled(IMAGE_PICK) && !enabled(EDIT_MENU),"empty state actions");
            shot(L"empty.bmp");
            SetFocus(GetDlgItem(editor,SEARCH_PLUGINS));
            MSG esc{};esc.hwnd=GetDlgItem(editor,SEARCH_PLUGINS);esc.message=WM_KEYDOWN;esc.wParam=VK_ESCAPE;
            check(SendMessageW(esc.hwnd,WM_GETDLGCODE,esc.wParam,reinterpret_cast<LPARAM>(&esc))&DLGC_WANTMESSAGE,"Esc must survive dialog navigation");
            check(IsDialogMessageW(editor,&esc),"dialog must route Esc to search");
            check(count()==2 && text(editor,SEARCH_PLUGINS).empty() && text(editor,VENDORS)==L"FabFilter","Esc clears search only");
            SetDlgItemTextW(editor,SEARCH_PLUGINS,L"Pro-Q");command(SEARCH_CLEAR,BN_CLICKED);
            check(count()==2 && text(editor,FILTER_SUMMARY)==L"FabFilter · 均衡","clear button retains both dimensions");
            SendDlgItemMessageW(editor,RESULTS,LB_SETSEL,TRUE,-1);command(RESULTS,LBN_SELCHANGE);
            SetDlgItemTextW(editor,VALUE,L"人声");check(enabled(EDIT_MENU),"batch edit enabled with value and selection");
            SetDlgItemTextW(editor,VALUE,L"   ");check(!enabled(EDIT_MENU),"blank edit value disabled");
            SetDlgItemTextW(editor,VALUE,L"人声");SetDlgItemTextW(editor,SEARCH_PLUGINS,L"Pro-Q");
            check(SendDlgItemMessageW(editor,RESULTS,LB_GETSELCOUNT,0,0)==2,"filter retains both distinct presets");
            shot(L"filtered.bmp");
            command(FILTER_RESET,BN_CLICKED);
            check(count()==18 && text(editor,VENDORS)==L"全部厂商" && text(editor,SEARCH_PLUGINS).empty(),"reset all conditions");
            select(VENDORS,L"Acme");check(count()==1,"vendor switch preserves only matching records");
            select(PURPOSES,L"合成器");check(count()==0,"purpose change retains vendor");
            command(FILTER_RESET,BN_CLICKED);SetDlgItemTextW(editor,SEARCH_PLUGINS,L"Unidentified");
            check(count()==1 && text(editor,PREVIEW_META).find(L"类型未确认")!=std::wstring::npos,"unknown kind must not become an instrument");
            check(text(editor,PREVIEW_TAGS)==L"待分类" && text(editor,PREVIEW_SOURCE)==L"暂无图片","unknown metadata and missing artwork");
            shot(L"unknown.bmp");
            // Check the actual resource layout, including changes at dialog scale.
            RECT client{};GetClientRect(editor,&client);
            for(HWND child=GetWindow(editor,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
                if(!IsWindowVisible(child) || GetDlgCtrlID(child)==VENDORS) continue;
                RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,editor,reinterpret_cast<POINT*>(&r),2);
                check(r.left>=0 && r.top>=0 && r.right<=client.right && r.bottom<=client.bottom,"control outside editor");
            }
            plugin_sandbox::reduce_motion=true;
            SendMessageW(editor,WM_SETTINGCHANGE,0,0);
            command(FILTER_RESET,BN_CLICKED);pump(editor,50);
            check(count()==18 && enabled(ADD_PLUGIN),"reduced motion keeps actions available");
            gui->destroy(plugin);DestroyWindow(parent);
            // No catalog: actions remain safe and the first-use path stays visible.
            fs::path empty=sandbox.path/L"empty";plugin_sandbox::isolate(module,empty);
            check(gui->create(plugin,CLAP_WINDOW_API_WIN32,false),"reopen empty editor");
            parent=CreateWindowExW(0,L"STATIC",L"Empty catalog",WS_POPUP,-10000,-10000,width,height,nullptr,nullptr,nullptr,nullptr);
            window.win32=parent;check(gui->set_parent(plugin,&window) && gui->show(plugin),"empty editor parent");
            editor=GetWindow(parent,GW_CHILD);ShowWindow(parent,SW_SHOWNOACTIVATE);
            check(count()==0 && !enabled(ADD_PLUGIN) && enabled(SCAN),"first-use actions");
            shot(L"first-use.bmp");
        }
        gui->destroy(plugin);if(IsWindow(parent)) DestroyWindow(parent);
        plugin->destroy(plugin);entry->deinit();FreeLibrary(module);
        std::cout<<"Native panel UI checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
