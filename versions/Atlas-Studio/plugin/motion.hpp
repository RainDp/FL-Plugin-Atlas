#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace motion {
constexpr UINT frame_ms=16;
constexpr double hover_in=120, hover_out=180, press_ms=75;
constexpr double select_ms=150, preview_ms=220;

inline bool enabled() {
    BOOL animate=FALSE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animate,0) && animate;
}
inline COLORREF mix(COLORREF from,COLORREF to,double t) {
    t=std::clamp(t,0.,1.);
    auto channel=[t](int a,int b) {return int(std::lround(a+(b-a)*t));};
    return RGB(channel(GetRValue(from),GetRValue(to)),channel(GetGValue(from),GetGValue(to)),
               channel(GetBValue(from),GetBValue(to)));
}

// Interrupted transitions continue from the currently displayed value.
class Tween {
public:
    explicit Tween(double value=0):current(value),start(value),target(value) {}
    double value() const { return current; }
    bool moving() const { return current!=target; }
    void finish() { current=start=target; }
    void reset(double value) { current=start=target=value; }
    void to(double value,double milliseconds,bool animate) {
        if(value==target) {if(!animate) finish(); return;}
        tick(GetTickCount64());
        start=current; target=value; began=GetTickCount64(); duration=milliseconds;
        if(!animate) finish();
    }
    bool tick(ULONGLONG now) {
        if(!moving()) return false;
        double t=std::clamp(double(now-began)/duration,0.,1.);
        double remaining=1.-t;
        current=t>=1.?target:start+(target-start)*(1.-remaining*remaining*remaining);
        return moving();
    }
private:
    double current=0,start=0,target=0,duration=1;
    ULONGLONG began=0;
};

class Button {
public:
    void attach(HWND window);
    static const Button* get(HWND window);
    Tween hover, press;
private:
    HWND window=nullptr;
    bool inside=false,animate=true,ticking=false;
    void refresh();
    void settle();
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
};
}
