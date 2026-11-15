#include "studio_surface.hpp"
#include "palette.hpp"
#include <initializer_list>

void StudioSurface::clear() { if(pattern) DeleteObject(pattern); pattern=nullptr; }

void StudioSurface::build(HWND dialog) {
    clear();
    RECT area{}; GetClientRect(dialog,&area);
    HDC screen=GetDC(dialog),dc=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right,area.bottom);
    if(!dc || !bitmap) {
        if(bitmap) DeleteObject(bitmap);
        if(dc) DeleteDC(dc);
        if(screen) ReleaseDC(dialog,screen);
        return;
    }
    auto old=SelectObject(dc,bitmap);
    auto fill=[&](RECT r,COLORREF color) {
        SetDCBrushColor(dc,color); FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    };
    auto units=[&](RECT r) { MapDialogRect(dialog,&r); return r; };
    fill(area,palette::background);
    fill(units({0,62,138,366}),palette::sidebar);
    fill(units({438,62,724,366}),palette::inspector);
    fill(units({0,366,724,416}),palette::footer);
    for(auto r:{RECT{0,62,724,62},RECT{0,366,724,366},RECT{0,395,724,395}}) {
        r=units(r); r.bottom=r.top+1; fill(r,palette::divider);
    }
    for(auto r:{RECT{138,62,138,366},RECT{438,62,438,366}}) {
        r=units(r); r.right=r.left+1; fill(r,palette::divider);
    }
    auto box=[&](RECT r) {
        r=units(r);
        SelectObject(dc,GetStockObject(DC_BRUSH));SetDCBrushColor(dc,palette::input_surface);
        SelectObject(dc,GetStockObject(DC_PEN));SetDCPenColor(dc,palette::edge);
        RoundRect(dc,r.left,r.top,r.right,r.bottom,8,8);
    };
    box({154,107,420,134});
    box({88,372,304,391});
    pattern=CreatePatternBrush(bitmap);
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(dialog,screen);
}

HBRUSH StudioSurface::brush_for(HDC dc,HWND child,HWND dialog) const {
    POINT offset{};
    if(child && child!=dialog) MapWindowPoints(child,dialog,&offset,1);
    POINT viewport{}; LPtoDP(dc,&viewport,1);
    SetBrushOrgEx(dc,viewport.x-offset.x,viewport.y-offset.y,nullptr);
    return pattern?pattern:static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));
}
void StudioSurface::paint(HDC dc,HWND child,HWND dialog,RECT region) const {
    FillRect(dc,&region,brush_for(dc,child,dialog));
}
