#include "motion.hpp"
#include <commctrl.h>
#include <windowsx.h>

namespace motion {
namespace {
constexpr UINT_PTR timer=0xA71A;
constexpr wchar_t state_prop[]=L"FLPluginAtlas.Motion";
}
void Button::attach(HWND w) {
    window=w; inside=false; animate=enabled(); ticking=false;
    hover.reset(0); press.reset(0);
    SetPropW(w,state_prop,reinterpret_cast<HANDLE>(this));
    SetWindowSubclass(w,proc,1,reinterpret_cast<DWORD_PTR>(this));
}
const Button* Button::get(HWND w) {
    return reinterpret_cast<Button*>(GetPropW(w,state_prop));
}
void Button::refresh() {
    animate=enabled();
    if(!IsWindowEnabled(window)) inside=false;
    hover.to(inside?1.:0.,inside?hover_in:hover_out,animate);
    bool pushed=(SendMessageW(window,BM_GETSTATE,0,0)&BST_PUSHED)!=0;
    press.to(pushed?1.:0.,press_ms,animate);
    if(!animate) settle();
    if((hover.moving() || press.moving()) && !ticking)
        if(!(ticking=SetTimer(window,timer,frame_ms,nullptr)!=0)) settle();
    InvalidateRect(window,nullptr,FALSE);
}
void Button::settle() {
    hover.finish(); press.finish();
    KillTimer(window,timer);
    ticking=false;
}
LRESULT CALLBACK Button::proc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto& state=*reinterpret_cast<Button*>(data);
    if(msg==WM_TIMER && wp==timer) {
        const auto now=GetTickCount64();
        bool moving=state.hover.tick(now);
        moving=state.press.tick(now) || moving;
        if(!IsWindowVisible(w)) {
            state.inside=false;state.hover.reset(0);state.press.reset(0);
            state.settle();
        } else if(!state.animate || !moving) state.settle();
        InvalidateRect(w,nullptr,FALSE);
        return 0;
    }
    auto result=DefSubclassProc(w,msg,wp,lp);
    if(msg!=WM_NCDESTROY && !state.window) return result;
    if(msg==WM_MOUSEMOVE && IsWindowEnabled(w)) {
        if(!state.inside) {
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};
            if(!TrackMouseEvent(&track)) return result;
            state.inside=true;
        }
        state.refresh();
    } else if(msg==WM_MOUSELEAVE) {
        state.inside=false;
        state.refresh();
    } else if(msg==BM_SETSTATE || msg==WM_LBUTTONDOWN || msg==WM_LBUTTONUP ||
              msg==WM_CAPTURECHANGED || msg==WM_KEYDOWN || msg==WM_KEYUP || msg==WM_ENABLE) {
        state.refresh();
    } else if(msg==WM_SETTINGCHANGE) {
        state.refresh();
        if(!state.animate) state.settle();
    } else if(msg==WM_SHOWWINDOW && !wp) {
        state.inside=false; state.hover.reset(0); state.press.reset(0); state.settle();
    } else if(msg==WM_NCDESTROY) {
        state.settle();
        RemoveWindowSubclass(w,proc,1);
        RemovePropW(w,state_prop);
        state.window=nullptr;
    }
    return result;
}
}
