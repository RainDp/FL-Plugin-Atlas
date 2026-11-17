#include "glass.hpp"
#include "palette.hpp"
#include "ids.h"
#include "layout.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
struct Light {
    double r,g,b;
    Light toward(Light other,double t) const {
        t=std::clamp(t,0.,1.);
        return {r+(other.r-r)*t,g+(other.g-g)*t,b+(other.b-b)*t};
    }
    uint32_t pixel() const {
        return (uint32_t(r+.5)<<16)|(uint32_t(g+.5)<<8)|uint32_t(b+.5);
    }
};
struct Texture {
    std::vector<uint32_t> pixels;
    int width=0,height=0;
    explicit Texture(HWND dialog) {
        auto module=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog,GWLP_HINSTANCE));
        auto bmp=static_cast<HBITMAP>(LoadImageW(module,MAKEINTRESOURCEW(GLASS_TEXTURE),IMAGE_BITMAP,0,0,LR_CREATEDIBSECTION));
        if(!bmp) return;
        BITMAP bm{};
        GetObjectW(bmp,sizeof(bm),&bm);
        width=bm.bmWidth; height=bm.bmHeight;
        BITMAPINFO info{};
        auto& b=info.bmiHeader;
        b.biSize=sizeof(b); b.biWidth=width; b.biHeight=-height; b.biPlanes=1; b.biBitCount=32;
        pixels.resize(size_t(width)*height);
        HDC dc=GetDC(dialog);
        if(!dc || GetDIBits(dc,bmp,0,height,pixels.data(),&info,DIB_RGB_COLORS)!=height) pixels.clear();
        if(dc) ReleaseDC(dialog,dc);
        DeleteObject(bmp);
    }
    Light sample(int x,int y) const {
        if(pixels.empty()) return {25,25,33};
        auto c=pixels[size_t(std::clamp(y,0,height-1))*width+std::clamp(x,0,width-1)];
        return {double((c>>16)&255),double((c>>8)&255),double(c&255)};
    }
};
double clamp_unit(double n) {
    return std::clamp(n,0.,1.);
}
double distance(double x,double y,const RECT& r,double radius) {
    double dx=std::abs(x-(r.left+r.right)*.5)-(r.right-r.left)*.5+radius;
    double dy=std::abs(y-(r.top+r.bottom)*.5)-(r.bottom-r.top)*.5+radius;
    return std::hypot(std::max(0.,dx),std::max(0.,dy))+std::min(std::max(dx,dy),0.)-radius;
}
}
void GlassSurface::clear() { if(pattern) DeleteObject(pattern); pattern=nullptr; }

void GlassSurface::build(HWND dialog) {
    clear();
    RECT area{}; GetClientRect(dialog,&area);
    int w=area.right,h=area.bottom;
    if(w<=0 || h<=0) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=w;
    info.bmiHeader.biHeight=-h; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* data=nullptr;
    auto bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&data,nullptr,0);
    if(!bitmap) return;
    RECT panes[]={{126,57,322,76},{174,315,323,335},{8,6,608,337}};
    for(auto& r:panes) {MapDialogRect(dialog,&r);layout::scale_rect(r);}
    RECT divisions{116,32,331,308};MapDialogRect(dialog,&divisions);layout::scale_rect(divisions);
    auto pixels=static_cast<uint32_t*>(data);
    const Texture texture(dialog);
    const double sx=double(texture.width)/w,sy=double(texture.height)/h;
    const Light deep{double(GetRValue(palette::background)),double(GetGValue(palette::background)),
                     double(GetBValue(palette::background))},ice{244,238,255};
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        int tx=int(x*sx),ty=int(y*sy);
        Light color=texture.sample(tx,ty).toward(deep,.20);
        for(int i=0;i<3;++i) {
            const auto& r=panes[i];
            if(x<r.left-3 || x>=r.right+3 || y<r.top-3 || y>=r.bottom+4) continue;
            double radius=layout::scale(i<2?9:18);
            double d=distance(x+.5,y+.5,r,radius);
            if(d>0) {
                color=color.toward(deep,.24*clamp_unit(1.-d/4));
                continue;
            }
            double u=double(x-r.left)/(r.right-r.left),v=double(y-r.top)/(r.bottom-r.top);
            // Shift the underlying scene most at the meniscus. The center stays legible.
            double bend=clamp_unit(1.+d/11.);
            int rx=tx+int((.5-u)*(5.+20.*bend)*sx),ry=ty+int((.5-v)*(5.+20.*bend)*sy);
            auto filtered=texture.sample(rx-3,ry).toward(texture.sample(rx+3,ry),.5)
                .toward(texture.sample(rx,ry-3).toward(texture.sample(rx,ry+3),.5),.5);
            auto pane=filtered.toward({40,40,49},.42);
            if(x<divisions.left && y>divisions.top && y<divisions.bottom)
                pane=pane.toward({14,15,20},.34);
            if(x>divisions.right && y>divisions.top && y<divisions.bottom)
                pane=pane.toward({42,38,54},.20);
            if(y<divisions.top) pane=pane.toward(ice,.015*(1.-u));
            if(i<2) pane={double(GetRValue(palette::input_surface)),double(GetGValue(palette::input_surface)),double(GetBValue(palette::input_surface))};
            double highlight=clamp_unit(1.-u*.70-v*.65);
            double bevel=clamp_unit(1.+d/5.);
            pane=pane.toward(ice,(i==2?.010*(1.-v):0.)+bevel*bevel*(.010+.060*highlight));
            auto rim=pane.toward(ice,.07+.21*highlight+.025*u*v);
            color=color.toward(rim.toward(pane,-d-.45),.5-d);
            break;
        }
        if((y==divisions.top && x>=panes[2].left && x<panes[2].right) ||
           (y==divisions.bottom && x>=divisions.left && x<panes[2].right))
            color=color.toward(ice,.10);
        if((x==divisions.left && y>divisions.top && y<panes[2].bottom) ||
           (x==divisions.right && y>divisions.top && y<divisions.bottom))
            color=color.toward(ice,.13);
        pixels[y*w+x]=color.pixel();
    }
    pattern=CreatePatternBrush(bitmap);
    DeleteObject(bitmap);
}

HBRUSH GlassSurface::brush_for(HDC dc,HWND child,HWND dialog) const {
    POINT offset{};
    if(child && child!=dialog) MapWindowPoints(child,dialog,&offset,1);
    POINT viewport{};
    LPtoDP(dc,&viewport,1);
    SetBrushOrgEx(dc,viewport.x-offset.x,viewport.y-offset.y,nullptr);
    return pattern?pattern:static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));
}
void GlassSurface::paint(HDC dc,HWND child,HWND dialog,RECT region) const {
    FillRect(dc,&region,brush_for(dc,child,dialog));
}
