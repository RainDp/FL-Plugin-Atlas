#include <clap/clap.h>
#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include "plugin_sandbox.hpp"
#include "../plugin/ids.h"
#include "../plugin/motion.hpp"
#include "flcat/store.hpp"
#include "flcat/paths.hpp"

namespace fs=std::filesystem;
void check(bool value,const char* text) {if(!value) throw std::runtime_error(text);}
void pump(unsigned duration) {
    auto until=GetTickCount64()+duration;
    do { MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);} Sleep(1); }
    while(GetTickCount64()<until);
}
void capture(HWND panel,const fs::path& file) {
    RECT r{};GetClientRect(panel,&r);
    auto screen=GetDC(panel),dc=CreateCompatibleDC(screen);
    auto bitmap=CreateCompatibleBitmap(screen,r.right,r.bottom);
    auto old=SelectObject(dc,bitmap);
    SendMessageW(panel,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND|PRF_NONCLIENT);
    GdiFlush();
    Gdiplus::GdiplusStartupInput startup;ULONG_PTR token=0;
    check(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok,"PNG startup");
    {
        Gdiplus::Bitmap image(bitmap,nullptr);
        CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        check(image.Save(file.c_str(),&png,nullptr)==Gdiplus::Ok,"save PNG");
    }
    Gdiplus::GdiplusShutdown(token);
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(panel,screen);
}
static unsigned timers=0;
LRESULT CALLBACK monitor(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_TIMER) ++timers;
    // The hidden QA host has no physical pointer inside it. Keep synthetic
    // pointer observations stable until the test explicitly leaves (wp=1).
    if(msg==WM_MOUSELEAVE && wp!=1) return 0;
    return DefSubclassProc(w,msg,wp,lp);
}
std::wstring caption(HWND w,int id) {
    wchar_t text[1024]{};GetDlgItemTextW(w,id,text,1024);return text;
}
LRESULT CALLBACK host_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_CLOSE) {DestroyWindow(w);return 0;}
    if(msg==WM_DESTROY) {PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
LRESULT CALLBACK preview_guard(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_COMMAND && HIWORD(wp)==BN_CLICKED) {
        auto id=LOWORD(wp);
        if(id==SCAN || id==BROWSE || id==FILL_IMAGES || id==ADD_PLUGIN || id==IMAGE_PICK || id==IMAGE_RESET) {
            SetDlgItemTextW(w,STATUS,L"当前为样本界面预览。实际扫描与加载请在 FL Studio 中打开 Studio CLAP。");
            return 0;
        }
    }
    return DefSubclassProc(w,msg,wp,lp);
}
int run_studio_preview(int argc,char** argv) {
    try {
        check(argc>=2,"supply CLAP binary");
        plugin_sandbox::Temporary sandbox;
        auto output=argc>=3?fs::absolute(fs::u8path(argv[2])):sandbox.path/L"shots";
        fs::create_directories(output);
        const bool interactive=argc>=4 && std::string(argv[3])=="--interactive";
        const auto library=sandbox.path/L"Documents/Image-Line/FL Studio/Presets/Plugin database";
        fs::create_directories(library);
        std::vector<flcat::Entry> entries={
            {"Effects/Pro-Q 3.fst","q3","FabFilter Pro-Q 3","Effects","VST3","FabFilter",{{u8"均衡","Fx|EQ"},{u8"母带","manual"}}},
            {"Effects/Pro-C 2.fst","c2","FabFilter Pro-C 2","Effects","VST3","FabFilter",{{u8"压缩","Fx|Dynamics"},{u8"人声","manual"}}},
            {"Effects/Pro-R 2.fst","r2","FabFilter Pro-R 2","Effects","VST3","FabFilter",{{u8"混响","Fx|Reverb"}}},
            {"Effects/Pro-L 2.fst","l2","FabFilter Pro-L 2","Effects","VST3","FabFilter",{{u8"母带","manual"},{u8"限制器","Fx|Limiter"}}},
            {"Effects/Pro-DS.fst","ds","FabFilter Pro-DS","Effects","VST3","FabFilter",{{u8"人声","manual"}}},
            {"Effects/Pro-MB.fst","mb","FabFilter Pro-MB","Effects","VST3","FabFilter",{{u8"动态处理","Fx|Dynamics"},{u8"母带","manual"}}},
            {"Effects/ValhallaVintageVerb.fst","vvv","ValhallaVintageVerb","Effects","VST3","Valhalla DSP",{{u8"混响","Fx|Reverb"}}},
            {"Effects/ValhallaDelay.fst","vd","ValhallaDelay","Effects","VST3","Valhalla DSP",{{u8"延迟","Fx|Delay"}}},
            {"Generators/Serum.fst","serum","Serum","Generators","VST3","Xfer Records",{{u8"合成器","Instrument|Synth"}}},
            {"Generators/Kontakt.fst","kontakt","Kontakt 7","Generators","VST3","Native Instruments",{{u8"采样器","Instrument|Sampler"}}},
            {"Effects/Other EQ.fst","other-q","FabFilter Pro-Q 3","Effects","VST","Other",{{u8"均衡","Fx|EQ"}}},
            {"Effects/Unidentified.fst","unknown","Unidentified Plugin With A Long Preset Name","Effects","VST","",{}}
        };
        for(const auto& entry:entries) {
            auto path=library/fs::u8path(entry.path);fs::create_directories(path.parent_path());
            std::ofstream(path,std::ios::binary)<<"isolated preset fixture";
        }
        auto db=sandbox.path/L"LocalAppData/FL Plugin Atlas/catalog.sqlite3";
        flcat::Store(db).replace_scan(flcat::path_id(library),entries);
        HMODULE module=LoadLibraryW(fs::absolute(fs::u8path(argv[1])).c_str());check(module,"load CLAP");
        plugin_sandbox::isolate(module,sandbox.path);
        auto api=reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(module,"clap_entry"));
        check(api && api->init(argv[1]),"entry init");
        auto factory=static_cast<const clap_plugin_factory_t*>(api->get_factory(CLAP_PLUGIN_FACTORY_ID));
        clap_host_t host{};host.clap_version=CLAP_VERSION;host.name="Atlas Studio UI verification";host.version="1";
        auto plugin=factory->create_plugin(factory,&host,factory->get_plugin_descriptor(factory,0)->id);
        check(plugin && plugin->init(plugin),"plugin init");
        auto gui=static_cast<const clap_plugin_gui_t*>(plugin->get_extension(plugin,CLAP_EXT_GUI));
        check(gui->create(plugin,CLAP_WINDOW_API_WIN32,false),"editor create");
        uint32_t width=0,height=0;check(gui->get_size(plugin,&width,&height),"editor size");
        WNDCLASSW cls{};cls.lpfnWndProc=host_proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"AtlasStudioPreview";cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
        RegisterClassW(&cls);
        RECT bounds{0,0,LONG(width),LONG(height)};
        AdjustWindowRectEx(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0);
        HWND parent=CreateWindowExW(0,cls.lpszClassName,L"FL Plugin Atlas Studio — 样本预览",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
            interactive?60:-10000,interactive?30:-10000,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,cls.hInstance,nullptr);
        clap_window_t window{};window.api=CLAP_WINDOW_API_WIN32;window.win32=parent;
        check(gui->set_parent(plugin,&window) && gui->show(plugin),"embed editor");
        ShowWindow(parent,SW_SHOWNOACTIVATE);
        HWND panel=GetWindow(parent,GW_CHILD),list=GetDlgItem(panel,RESULTS),purposes=GetDlgItem(panel,PURPOSES),vendors=GetDlgItem(panel,VENDORS);
        for(int id:{SCAN,ADD_PLUGIN,TYPE_ALL,TYPE_EFFECTS,TYPE_INSTRUMENTS,RESULTS,PURPOSES}) SetWindowSubclass(GetDlgItem(panel,id),monitor,9,0);
        SetWindowSubclass(panel,monitor,9,0);
        auto type=[&](int id) {SendMessageW(panel,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(panel,id)));};
        auto vendor=[&](const wchar_t* name) {
            int n=int(SendMessageW(vendors,CB_FINDSTRINGEXACT,-1,reinterpret_cast<LPARAM>(name)));check(n>=0,"vendor fixture");
            SendMessageW(vendors,CB_SETCURSEL,n,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(VENDORS,CBN_SELCHANGE),reinterpret_cast<LPARAM>(vendors));
        };
        auto purpose=[&](const wchar_t* name) {
            int n=int(SendMessageW(purposes,LB_FINDSTRINGEXACT,-1,reinterpret_cast<LPARAM>(name)));check(n>=0,"purpose fixture");
            SendMessageW(purposes,LB_SETCURSEL,n,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(PURPOSES,LBN_SELCHANGE),reinterpret_cast<LPARAM>(purposes));
        };
        auto count=[&] {return int(SendMessageW(list,LB_GETCOUNT,0,0));};
        auto settle=[&] {pump(260);RedrawWindow(panel,nullptr,nullptr,RDW_UPDATENOW|RDW_ALLCHILDREN|RDW_INVALIDATE);};
        if(!interactive) {
        check(count()==12,"all entries must remain independent");
        check(!IsWindowEnabled(GetDlgItem(panel,IMAGE_RESET)),"default image reset disabled");
        vendor(L"FabFilter");check(count()==6,"vendor filter");
        purpose(L"均衡");check(count()==1 && caption(panel,PREVIEW_NAME)==L"FabFilter Pro-Q 3","combined filter");
        purpose(L"母带");check(count()==3 && caption(panel,VENDORS)==L"FabFilter","purpose replaces only purpose");
        type(TYPE_INSTRUMENTS);check(count()==0 && caption(panel,VENDORS)==L"FabFilter","type intersection");
        settle();capture(panel,output/L"empty-filter.png");
        type(TYPE_ALL);purpose(L"全部");vendor(L"全部厂商");type(TYPE_INSTRUMENTS);check(count()==2,"instrument filter");
        settle();capture(panel,output/L"instruments.png");
        type(TYPE_ALL);vendor(L"未知厂商");check(count()==1,"unknown vendor available");
        settle();capture(panel,output/L"long-name.png");
        vendor(L"全部厂商");SetDlgItemTextW(panel,SEARCH_PLUGINS,L"pro-q");check(count()==2,"case insensitive search retains distinct identities");
        SetDlgItemTextW(panel,SEARCH_PLUGINS,L"no matching plugin");check(count()==0,"search empty state");
        SetFocus(GetDlgItem(panel,SEARCH_PLUGINS));
        check(GetFocus()==GetDlgItem(panel,SEARCH_PLUGINS),"search accepts keyboard focus");
        settle();capture(panel,output/L"search-focus.png");
        SendDlgItemMessageW(panel,SEARCH_PLUGINS,WM_KEYDOWN,VK_ESCAPE,0);
        check(caption(panel,SEARCH_PLUGINS).empty() && count()==12,"Escape clears search");
        SetFocus(parent);
        SetDlgItemTextW(panel,SEARCH_PLUGINS,L"");check(count()==12,"clear search");
        vendor(L"FabFilter");
        // Validate bulk selection without editing the fixture catalog.
        SendMessageW(list,LB_SETSEL,TRUE,1);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(RESULTS,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
        check(caption(panel,SELECTION_COUNT).find(L"2")!=std::wstring::npos,"bulk count tracks actual selection");
        int eq=int(SendMessageW(list,LB_FINDSTRING,-1,reinterpret_cast<LPARAM>(L"FabFilter Pro-Q 3")));
        check(eq>=0,"EQ snapshot fixture");
        SendMessageW(list,LB_SETSEL,FALSE,-1);SendMessageW(list,LB_SETSEL,TRUE,eq);
        SendMessageW(list,LB_SETCARETINDEX,eq,FALSE);
        SendMessageW(panel,WM_COMMAND,MAKEWPARAM(RESULTS,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
        settle();capture(panel,output/L"studio.png");
        auto button=GetDlgItem(panel,SCAN);
        auto state=reinterpret_cast<const motion::Button*>(GetPropW(button,L"FLPluginAtlas.Motion"));check(state,"button motion state");
        SendMessageW(button,WM_MOUSEMOVE,0,MAKELPARAM(8,10));pump(35);
        check(state->hover.value()>0 && state->hover.value()<1,"hover interpolates");
        SendMessageW(button,BM_SETSTATE,TRUE,0);
        check(state->press.moving() && (SendMessageW(button,BM_GETSTATE,0,0)&BST_PUSHED),"press transition starts");
        pump(120);check(state->press.value()==1,"press settles at pushed state");
        SendMessageW(button,BM_SETSTATE,FALSE,0);
        SendMessageW(button,WM_MOUSELEAVE,1,0);pump(220);
        check(state->hover.value()==0,"interrupted hover settles");
        plugin_sandbox::reduce_motion=true;SendMessageW(panel,WM_SETTINGCHANGE,0,0);
        SendMessageW(button,WM_MOUSEMOVE,0,MAKELPARAM(8,10));
        check(state->hover.value()==1,"reduced motion snaps to final state");
        SendMessageW(button,WM_MOUSELEAVE,1,0);settle();
        unsigned idle=timers;pump(180);check(timers==idle,"no animation timers at idle");
        plugin_sandbox::reduce_motion=false;SendMessageW(panel,WM_SETTINGCHANGE,0,0);
        DWORD objects=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        for(int i=0;i<25;++i) {type(i%2?TYPE_EFFECTS:TYPE_ALL);pump(5);}
        settle();check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=objects+8,"GDI resources remain bounded");
        type(TYPE_ALL);settle();
        std::cout<<"Studio UI: isolated library, independent filters, unknown vendor, search, selection, reduced motion, idle and GDI checks passed. "<<width<<"x"<<height<<"\n";
        SetWindowSubclass(panel,preview_guard,10,0);
        type(ADD_PLUGIN);check(caption(panel,STATUS).find(L"样本界面预览")!=std::wstring::npos,"preview blocks fixture loading");
        type(SCAN);check(IsWindowEnabled(GetDlgItem(panel,SCAN)),"preview must not start a scan");
        RemoveWindowSubclass(panel,preview_guard,10);
        }
        if(interactive) {
            for(int id:{SCAN,ADD_PLUGIN,TYPE_ALL,TYPE_EFFECTS,TYPE_INSTRUMENTS,RESULTS,PURPOSES}) RemoveWindowSubclass(GetDlgItem(panel,id),monitor,9);
            RemoveWindowSubclass(panel,monitor,9);
            SetWindowSubclass(panel,preview_guard,10,0);
            vendor(L"FabFilter");
            int eq=int(SendMessageW(list,LB_FINDSTRING,-1,reinterpret_cast<LPARAM>(L"FabFilter Pro-Q 3")));
            SendMessageW(list,LB_SETSEL,FALSE,-1);SendMessageW(list,LB_SETSEL,TRUE,eq);
            SendMessageW(list,LB_SETCARETINDEX,eq,FALSE);
            SendMessageW(panel,WM_COMMAND,MAKEWPARAM(RESULTS,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
            ShowWindow(parent,SW_SHOW);SetForegroundWindow(parent);
            MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0) {if(!IsDialogMessageW(panel,&msg)) {TranslateMessage(&msg);DispatchMessageW(&msg);}}
        }
        gui->destroy(plugin);plugin->destroy(plugin);api->deinit();FreeLibrary(module);
        if(IsWindow(parent)) DestroyWindow(parent);
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}

#ifdef ATLAS_STUDIO_PREVIEW
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    auto folder=fs::path(executable).parent_path();
    std::string binary=(folder/L"FL Plugin Atlas Studio.clap").u8string();
    std::string output=(folder/L"review").u8string();
    char self[]="Atlas Studio Preview",interactive[]="--interactive";
    char* args[]={self,binary.data(),output.data(),interactive};
    return run_studio_preview(4,args);
}
#else
int main(int argc,char** argv) {return run_studio_preview(argc,argv);}
#endif
