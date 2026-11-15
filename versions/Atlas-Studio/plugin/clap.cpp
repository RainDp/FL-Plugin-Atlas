#include <clap/clap.h>
#include "panel.hpp"
#include <cstring>
#include <new>

namespace {
using Plugin=clap_plugin_t;
const char* const features[]={CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,CLAP_PLUGIN_FEATURE_UTILITY,nullptr};
const clap_plugin_descriptor_t descriptor={CLAP_VERSION,"org.flpluginatlas.catalog.studio","FL Plugin Atlas Studio",
    "FL Plugin Atlas","","","","0.2.0","插件图谱 · Studio 界面副本",features};
struct Instance { Plugin api{}; Panel panel; };
Panel& panel(const Plugin* p) {return static_cast<Instance*>(p->plugin_data)->panel;}
bool supported(const Plugin*,const char* api,bool floating) {
    return !floating && api && std::strcmp(api,CLAP_WINDOW_API_WIN32)==0;
}
bool size(const Plugin* p,uint32_t* w,uint32_t* h) {
    RECT r{};
    if(!GetClientRect(panel(p).window,&r)) return false;
    *w=r.right; *h=r.bottom;
    return true;
}
const clap_plugin_gui_t gui={
    supported,
    [](const Plugin*,const char** api,bool* floating){*api=CLAP_WINDOW_API_WIN32; *floating=false; return true;},
    [](const Plugin* p,const char* api,bool floating){return supported(p,api,floating) && !panel(p).window && panel(p).create();},
    [](const Plugin* p){panel(p).close();},
    [](const Plugin*,double){return false;},size,
    [](const Plugin*){return false;},
    [](const Plugin*,clap_gui_resize_hints_t*){return false;},
    size,
    [](const Plugin* p,uint32_t w,uint32_t h){uint32_t a,b; return size(p,&a,&b) && a==w && b==h;},
    [](const Plugin* p,const clap_window_t* w){
        if(!w || !supported(p,w->api,false) || !IsWindow(static_cast<HWND>(w->win32)) || !panel(p).window) return false;
        SetLastError(0);
        return SetParent(panel(p).window,static_cast<HWND>(w->win32)) || GetLastError()==0;
    },
    [](const Plugin*,const clap_window_t*){return false;}, [](const Plugin*,const char*){},
    [](const Plugin* p){if(!panel(p).window)return false; ShowWindow(panel(p).window,SW_SHOWNA); return true;},
    [](const Plugin* p){if(!panel(p).window)return false; ShowWindow(panel(p).window,SW_HIDE); return true;}
};
const clap_plugin_audio_ports_t ports={
    [](const Plugin*,bool)->uint32_t{return 1;},
    [](const Plugin*,uint32_t n,bool input,clap_audio_port_info_t* out){
        if(n!=0) return false;
        *out={};
        std::strcpy(out->name,input?"Stereo In":"Stereo Out");
        out->flags=CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS | CLAP_AUDIO_PORT_REQUIRES_COMMON_SAMPLE_SIZE;
        out->channel_count=2; out->port_type=CLAP_PORT_STEREO; out->in_place_pair=0;
        return true;
    }
};
template<class T> void passthrough(T** in,T** out,uint32_t frames) {
    for(int ch=0;ch<2;++ch) if(in[ch]!=out[ch]) std::memmove(out[ch],in[ch],frames*sizeof(T));
}
clap_process_status process(const Plugin*,const clap_process_t* block) {
    if(block->audio_inputs_count!=1 || block->audio_outputs_count!=1) return CLAP_PROCESS_ERROR;
    auto& in=block->audio_inputs[0]; auto& out=block->audio_outputs[0];
    if(in.channel_count!=2 || out.channel_count!=2) return CLAP_PROCESS_ERROR;
    if(in.data32 && out.data32) passthrough(in.data32,out.data32,block->frames_count);
    else if(in.data64 && out.data64) passthrough(in.data64,out.data64,block->frames_count);
    else return CLAP_PROCESS_ERROR;
    out.constant_mask=in.constant_mask;
    return CLAP_PROCESS_CONTINUE;
}
const clap_plugin_factory_t factory={
    [](const clap_plugin_factory_t*)->uint32_t{return 1;},
    [](const clap_plugin_factory_t*,uint32_t index){return index==0?&descriptor:nullptr;},
    [](const clap_plugin_factory_t*,const clap_host_t* host,const char* id)->const Plugin* {
        if(!host || !clap_version_is_compatible(host->clap_version) || !id || std::strcmp(id,descriptor.id)) return nullptr;
        auto* obj=new(std::nothrow) Instance;
        if(!obj) return nullptr;
        obj->api={&descriptor,obj,[](const Plugin*){return true;},
            [](const Plugin* p){delete static_cast<Instance*>(p->plugin_data);},
            [](const Plugin*,double,uint32_t,uint32_t){return true;},[](const Plugin*){},
            [](const Plugin*){return true;},[](const Plugin*){},[](const Plugin*){},process,
            [](const Plugin*,const char* ext)->const void* {
                if(!std::strcmp(ext,CLAP_EXT_GUI)) return &gui;
                if(!std::strcmp(ext,CLAP_EXT_AUDIO_PORTS)) return &ports;
                return nullptr;
            },[](const Plugin*){}};
        return &obj->api;
    }
};
}
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry={CLAP_VERSION,[](const char*){return true;},[]{},
    [](const char* id)->const void* {return std::strcmp(id,CLAP_PLUGIN_FACTORY_ID)==0?&factory:nullptr;}};
