#include "list_scroll.hpp"
#include "palette.hpp"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>

namespace {
constexpr UINT_PTR motion_timer=0xA71B;
void track_leave(HWND w) {
    TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,w,0};
    TrackMouseEvent(&track);
}
}
void ListScroll::attach(HWND list_window, HWND thumb) {
    list=list_window; rail=thumb;
    hovered_row=grab=-1; hot=false; wheel_delta=0;
    animate=motion::enabled(); ticking=false;
    rows.clear(); rail_hover.reset(0);
    auto ref=reinterpret_cast<DWORD_PTR>(this);
    SetWindowSubclass(list,list_proc,1,ref);
    SetWindowSubclass(rail,rail_proc,1,ref);
}
ListScroll::Range ListScroll::range() const {
    RECT r{}; GetClientRect(list,&r);
    int height=std::max(1L,r.bottom);
    int row=std::max<LRESULT>(1,SendMessageW(list,LB_GETITEMHEIGHT,0,0));
    int page=std::max(1,height/row);
    int count=static_cast<int>(SendMessageW(list,LB_GETCOUNT,0,0));
    int top=static_cast<int>(SendMessageW(list,LB_GETTOPINDEX,0,0));
    int limit=std::max(0,count-page);
    int thumb=std::clamp(MulDiv(height,page,std::max(count,1)),std::min(height,20),height);
    return {height,thumb,limit?MulDiv(std::clamp(top,0,limit),height-thumb,limit):0,limit};
}
void ListScroll::draw(HDC dc) const {
    auto bar=range();
    if(!bar.max_top) return;
    RECT r{}; GetClientRect(rail,&r);
    int x=r.right/2;
    auto prev=SelectObject(dc,GetStockObject(DC_BRUSH));
    auto pen=SelectObject(dc,GetStockObject(NULL_PEN));
    SetDCBrushColor(dc,motion::mix(palette::subdued,palette::signal,rail_hover.value()));
    int width=4+int(std::lround(2*rail_hover.value()));
    RoundRect(dc,x-width/2,bar.offset,x+(width+1)/2,bar.offset+bar.thumb,width,width);
    SelectObject(dc,pen); SelectObject(dc,prev);
}
void ListScroll::hover(int row,bool over) {
    if(hot!=over) {
        hot=over;
        rail_hover.to(hot?1.:0.,hot?motion::hover_in:motion::hover_out,animate);
        InvalidateRect(rail,nullptr,FALSE); wake();
    }
    if(hovered_row==row) return;
    for(int n:{hovered_row,row}) {
        auto found=rows.find(n);
        if(found!=rows.end()) found->second.hover.to(n==row?1.:0.,n==row?motion::hover_in:motion::hover_out,animate);
        invalidate(n);
    }
    hovered_row=row;
    wake();
}
void ListScroll::invalidate(int row) {
    RECT r{};
    if(row>=0 && SendMessageW(list,LB_GETITEMRECT,row,reinterpret_cast<LPARAM>(&r))!=LB_ERR)
        InvalidateRect(list,&r,FALSE);
}
ListScroll::Appearance ListScroll::appearance(int row,bool selected) {
    auto inserted=rows.try_emplace(row);
    auto& state=inserted.first->second;
    if(inserted.second) state.selection.reset(selected?1.:0.);
    state.selection.to(selected?1.:0.,motion::select_ms,animate);
    state.hover.to(row==hovered_row?1.:0.,row==hovered_row?motion::hover_in:motion::hover_out,animate);
    if(state.selection.moving() || state.hover.moving()) wake();
    return {state.hover.value(),state.selection.value()};
}
void ListScroll::wake() {
    if(!animate) {settle(); return;}
    if(!ticking && !(ticking=SetTimer(list,motion_timer,motion::frame_ms,nullptr)!=0)) settle();
}
void ListScroll::settle() {
    KillTimer(list,motion_timer); ticking=false;
    rail_hover.finish();
    for(auto& pair:rows) {pair.second.hover.finish();pair.second.selection.finish();}
}
void ListScroll::settings_changed() {
    animate=motion::enabled();
    if(!animate) settle();
    InvalidateRect(list,nullptr,FALSE); InvalidateRect(rail,nullptr,FALSE);
}
void ListScroll::frame() {
    auto now=GetTickCount64();
    bool moving=rail_hover.tick(now);
    if(moving || ticking) InvalidateRect(rail,nullptr,FALSE);
    RECT client{};GetClientRect(list,&client);
    for(auto it=rows.begin();it!=rows.end();) {
        RECT box{};
        if(SendMessageW(list,LB_GETITEMRECT,it->first,reinterpret_cast<LPARAM>(&box))==LB_ERR ||
           box.bottom<=0 || box.top>=client.bottom) {
            it=rows.erase(it);
            continue;
        }
        auto& row=it->second;
        bool changed=row.hover.moving() || row.selection.moving();
        moving=row.hover.tick(now) || moving;
        moving=row.selection.tick(now) || moving;
        if(changed) InvalidateRect(list,&box,FALSE);
        ++it;
    }
    if(!IsWindowVisible(list)) {
        hovered_row=-1;hot=false;rail_hover.reset(0);
        for(auto& pair:rows) pair.second.hover.reset(0);
        settle();
    } else if(!animate || !moving) settle();
}
void ListScroll::move(int top) {
    SendMessageW(list,LB_SETTOPINDEX,std::clamp(top,0,range().max_top),0);
}
LRESULT CALLBACK ListScroll::list_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto& s=*reinterpret_cast<ListScroll*>(data);
    if(msg==WM_TIMER && wp==motion_timer) {s.frame();return 0;}
    if((msg==WM_MOUSEMOVE || msg==WM_MOUSEWHEEL || msg==WM_KEYDOWN || msg==LB_SETCURSEL) &&
       s.animate!=motion::enabled()) s.settings_changed();
    if(msg==WM_NCDESTROY) {s.settle();s.rows.clear();RemoveWindowSubclass(w,list_proc,1);}
    if(msg==WM_SHOWWINDOW && !wp) {s.hover(-1,false);s.settle();}
    if(msg==WM_SETTINGCHANGE) s.settings_changed();
    if(msg==WM_MOUSEMOVE) {
        if(!s.hot) track_leave(w);
        auto n=SendMessageW(w,LB_ITEMFROMPOINT,0,lp);
        RECT row{}; POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        bool hit=!HIWORD(n) && SendMessageW(w,LB_GETITEMRECT,LOWORD(n),reinterpret_cast<LPARAM>(&row))!=LB_ERR && PtInRect(&row,p);
        s.hover(hit?LOWORD(n):-1,true);
    } else if(msg==WM_MOUSELEAVE) s.hover(-1,false);
    if(msg==LB_RESETCONTENT) {
        s.hover(-1,false);s.settle();s.rows.clear();s.wheel_delta=0;
    }
    if(msg==WM_MOUSEWHEEL) {
        UINT lines=3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
        if(lines==WHEEL_PAGESCROLL) {
            RECT r{}; GetClientRect(w,&r);
            lines=std::max<LRESULT>(1,r.bottom/std::max<LRESULT>(1,SendMessageW(w,LB_GETITEMHEIGHT,0,0)));
        }
        s.wheel_delta+=GET_WHEEL_DELTA_WPARAM(wp);
        int steps=s.wheel_delta/WHEEL_DELTA; s.wheel_delta%=WHEEL_DELTA;
        if(steps) {s.hover(-1,true); s.move(static_cast<int>(SendMessageW(w,LB_GETTOPINDEX,0,0))-steps*int(lines));}
        return 0;
    }
    auto result=DefSubclassProc(w,msg,wp,lp);
    if(msg==LB_SETTOPINDEX || msg==WM_KEYDOWN || msg==WM_VSCROLL) s.frame();
    if(msg==LB_SETTOPINDEX || msg==WM_KEYDOWN || msg==WM_VSCROLL || msg==LB_RESETCONTENT ||
       msg==LB_SETCARETINDEX || msg==LB_SETCURSEL || msg==WM_MOUSEMOVE || msg==WM_SETREDRAW)
        InvalidateRect(s.rail,nullptr,FALSE);
    return result;
}
LRESULT CALLBACK ListScroll::rail_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto& s=*reinterpret_cast<ListScroll*>(data);
    if(msg==WM_MOUSEWHEEL) return SendMessageW(s.list,msg,wp,lp);
    if(msg==WM_MOUSELEAVE) s.hover(-1,false);
    if(msg==WM_CAPTURECHANGED) {s.grab=-1;InvalidateRect(w,nullptr,FALSE);}
    if(msg==WM_LBUTTONUP && GetCapture()==w) {ReleaseCapture();return 0;}
    if(msg==WM_LBUTTONDOWN || msg==WM_MOUSEMOVE) {
        track_leave(w); s.hover(-1,true);
        auto bar=s.range(); int y=GET_Y_LPARAM(lp);
        if(msg==WM_LBUTTONDOWN && bar.max_top) {
            SetFocus(s.list); SetCapture(w);
            s.grab=y>=bar.offset && y<bar.offset+bar.thumb?y-bar.offset:bar.thumb/2;
        }
        if(s.grab>=0 && bar.length>bar.thumb)
            s.move(MulDiv(y-s.grab,bar.max_top,bar.length-bar.thumb));
        return 0;
    }
    return DefSubclassProc(w,msg,wp,lp);
}
