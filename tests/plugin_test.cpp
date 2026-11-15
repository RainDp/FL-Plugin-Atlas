#include <clap/clap.h>
#include <windows.h>
#include "../plugin/ids.h"
#include "../plugin/preset_drag.hpp"
#include <shellapi.h>
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>

static void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
static void check_drag_data() {
    namespace fs=std::filesystem;
    auto root=fs::temp_directory_path()/("atlas-drag-"+std::to_string(GetCurrentProcessId()));
    check(fs::create_directory(root),"create isolated drag fixture");
    struct Cleanup {fs::path path; ~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{root};
    check(SUCCEEDED(OleInitialize(nullptr)),"initialize OLE");
    for(auto kind:{"Effects","Generators"}) {
        auto relative=fs::path(kind)/L"预设 A.fst";
        fs::create_directory(root/kind);
        std::ofstream(root/relative) << kind;
        auto data=preset_drag_data(root,relative);
        FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
        STGMEDIUM medium{};
        check(SUCCEEDED(data->GetData(&format,&medium)),"expected shell file drop");
        auto drop=static_cast<HDROP>(medium.hGlobal);
        wchar_t path[32768]{};
        check(DragQueryFileW(drop,0xFFFFFFFF,nullptr,0)==1 && DragQueryFileW(drop,0,path,32768) &&
              fs::equivalent(root/relative,path),"drop must contain exactly the original preset");
        ReleaseStgMedium(&medium);
        data->Release();
    }
    for(auto bad:{L"absent.fst",L"wrong.dll",L"../outside.fst"}) {
        bool refused=false;
        try {preset_drag_data(root,bad)->Release();} catch(const std::runtime_error&){refused=true;}
        check(refused,"invalid preset accepted");
    }
    OleUninitialize();
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"supply plugin path");
        HMODULE module=LoadLibraryW(std::filesystem::absolute(argv[1]).c_str());
        check(module,"LoadLibrary failed");
        auto entry=reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(module,"clap_entry"));
        check(entry && entry->init(argv[1]),"entry init");
        auto factory=static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        check(factory && factory->get_plugin_count(factory)==1,"factory");
        auto descriptor=factory->get_plugin_descriptor(factory,0);
        clap_host_t host{}; host.clap_version=CLAP_VERSION; host.name="Atlas tests"; host.version="1";
        const auto* p=factory->create_plugin(factory,&host,descriptor->id);
        check(p && p->init(p),"instance");
        auto gui=static_cast<const clap_plugin_gui_t*>(p->get_extension(p,CLAP_EXT_GUI));
        check(gui && gui->create(p,CLAP_WINDOW_API_WIN32,false),"create editor");
        auto parent=CreateWindowExW(0,L"STATIC",L"Test host",WS_POPUP,0,0,1200,900,nullptr,nullptr,nullptr,nullptr);
        clap_window_t window{}; window.api=CLAP_WINDOW_API_WIN32; window.win32=parent;
        check(gui->set_parent(p,&window) && gui->show(p),"embed editor");
        uint32_t w=0,h=0;
        check(gui->get_size(p,&w,&h) && w>400 && h>250,"editor size");
        auto editor=GetWindow(parent,GW_CHILD);
        check(editor && GetDlgItem(editor,RESULTS) && GetDlgItem(editor,VENDORS),"missing catalog UI");
        wchar_t scan_label[32]{};
        GetDlgItemTextW(editor,SCAN,scan_label,32);
        check(std::wstring(scan_label)==L"扫描插件","incorrect resource encoding");
        HWND purposes=GetDlgItem(editor,PURPOSES);
        check(SendMessageW(purposes,LB_FINDSTRINGEXACT,-1,reinterpret_cast<LPARAM>(L"全部"))==0 &&
              SendMessageW(purposes,LB_GETCURSEL,0,0)==0,"all purposes should be first and selected");
        check(!(GetWindowLongPtrW(purposes,GWL_STYLE)&(LBS_MULTIPLESEL|LBS_EXTENDEDSEL)),"single purpose filter");
        auto choose=[&](int index) {
            check(SendMessageW(purposes,LB_SETCURSEL,index,0)==index,"select purpose");
            SendMessageW(editor,WM_COMMAND,MAKEWPARAM(PURPOSES,LBN_SELCHANGE),reinterpret_cast<LPARAM>(purposes));
        };
        int a=static_cast<int>(SendMessageW(purposes,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"甲")));
        int b=static_cast<int>(SendMessageW(purposes,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"乙")));
        choose(a);
        choose(b);
        check(SendMessageW(purposes,LB_GETCURSEL,0,0)==b,"new purpose must replace old purpose");
        choose(0);
        check(SendMessageW(purposes,LB_GETCURSEL,0,0)==0,"restore all");
        check_drag_data();
        check(gui->hide(p),"hide editor");
        gui->destroy(p);
        check(gui->create(p,CLAP_WINDOW_API_WIN32,false) && gui->set_parent(p,&window),"reopen editor");
        gui->destroy(p);
        DestroyWindow(parent);
        auto ports=static_cast<const clap_plugin_audio_ports_t*>(p->get_extension(p,CLAP_EXT_AUDIO_PORTS));
        clap_audio_port_info_t info{};
        check(ports && ports->get(p,0,true,&info) && info.channel_count==2,"stereo ports");
        check(p->activate(p,48000,1,64) && p->start_processing(p),"activate");
        float l[]={1,-0.25f},r[]={0,0.8f},ol[2]{},orr[2]{};
        float* inputs[]={l,r}; float* outputs[]={ol,orr};
        clap_audio_buffer_t in{},out{}; in.data32=inputs; out.data32=outputs;
        in.channel_count=out.channel_count=2;
        clap_process_t block{}; block.audio_inputs_count=block.audio_outputs_count=1;
        block.audio_inputs=&in; block.audio_outputs=&out; block.frames_count=2;
        check(p->process(p,&block)==CLAP_PROCESS_CONTINUE && ol[0]==1 && ol[1]==l[1] && orr[1]==r[1],"passthrough");
        p->stop_processing(p); p->deactivate(p); p->destroy(p);
        entry->deinit(); FreeLibrary(module);
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
