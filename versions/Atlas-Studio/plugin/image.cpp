#include "image.hpp"
#include "palette.hpp"
#include <wincodec.h>
#include <algorithm>
#include <memory>
#include <cmath>
#include <cstring>

namespace {
template<class T> struct Release { void operator()(T* p) const { if(p) p->Release(); } };
template<class T> using Com=std::unique_ptr<T,Release<T>>;
struct Apartment {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~Apartment(){if(SUCCEEDED(hr)) CoUninitialize();}
};
}
bool PreviewImage::load(const std::filesystem::path& file) {
    Apartment apartment;
    IWICImagingFactory* factoryPtr=nullptr;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factoryPtr)))) return false;
    Com<IWICImagingFactory> factory(factoryPtr);
    IWICBitmapDecoder* decoderPtr=nullptr;
    if(FAILED(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoderPtr))) return false;
    Com<IWICBitmapDecoder> decoder(decoderPtr);
    IWICBitmapFrameDecode* framePtr=nullptr;
    if(FAILED(decoder->GetFrame(0,&framePtr))) return false;
    Com<IWICBitmapFrameDecode> frame(framePtr);
    UINT w=0,h=0;
    if(FAILED(frame->GetSize(&w,&h)) || !w || !h || uint64_t(w)*h>64000000) return false;
    const double scale=std::min(1.0,960.0/std::max(w,h));
    w=std::max(1U,UINT(w*scale)); h=std::max(1U,UINT(h*scale));
    IWICBitmapScaler* scalerPtr=nullptr;
    if(FAILED(factory->CreateBitmapScaler(&scalerPtr))) return false;
    Com<IWICBitmapScaler> scaler(scalerPtr);
    if(FAILED(scaler->Initialize(frame.get(),w,h,WICBitmapInterpolationModeFant))) return false;
    IWICFormatConverter* converterPtr=nullptr;
    if(FAILED(factory->CreateFormatConverter(&converterPtr))) return false;
    Com<IWICFormatConverter> converter(converterPtr);
    if(FAILED(converter->Initialize(scaler.get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return false;
    std::vector<unsigned char> data(size_t(w)*h*4);
    if(FAILED(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(data.size()),data.data()))) return false;
    // Composite transparency onto the same charcoal used by the preview canvas.
    for(size_t i=0;i<data.size();i+=4){
        unsigned alpha=255-data[i+3];
        data[i]+=BYTE(GetBValue(palette::preview_canvas)*alpha/255);
        data[i+1]+=BYTE(GetGValue(palette::preview_canvas)*alpha/255);
        data[i+2]+=BYTE(GetRValue(palette::preview_canvas)*alpha/255);
    }
    pixels=std::move(data); width=w; height=h; return true;
}
void PreviewImage::draw(HDC dc, RECT r, BYTE opacity) const {
    if(pixels.empty()) return;
    int boxW=r.right-r.left,boxH=r.bottom-r.top;
    if(boxW<1 || boxH<1 || opacity==0) return;
    if(opacity<255) {
        HDC layer=CreateCompatibleDC(dc);
        HBITMAP bitmap=CreateCompatibleBitmap(dc,boxW,boxH);
        if(layer && bitmap) {
            auto old=SelectObject(layer,bitmap);
            RECT box{0,0,boxW,boxH};
            SetDCBrushColor(layer,palette::preview_canvas);
            FillRect(layer,&box,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            draw(layer,box);
            AlphaBlend(dc,r.left,r.top,boxW,boxH,layer,0,0,boxW,boxH,{AC_SRC_OVER,0,opacity,0});
            SelectObject(layer,old);
            DeleteObject(bitmap); DeleteDC(layer);
            return;
        }
        if(bitmap) DeleteObject(bitmap);
        if(layer) DeleteDC(layer);
    }
    double factor=std::min(double(boxW)/width,double(boxH)/height);
    int w=std::max(1,int(width*factor)),h=std::max(1,int(height*factor));
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    SetStretchBltMode(dc,HALFTONE); SetBrushOrgEx(dc,0,0,nullptr);
    StretchDIBits(dc,r.left+(boxW-w)/2,r.top+(boxH-h)/2,w,h,0,0,width,height,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
}

PreviewImage PreviewImage::capture(HWND window,int inset) {
    PreviewImage result;
    RECT area{};GetClientRect(window,&area);
    int w=area.right,h=area.bottom;
    if(w<1 || h<1) return result;
    BITMAPINFO info{};
    auto& header=info.bmiHeader;
    header.biSize=sizeof(header);header.biWidth=w;header.biHeight=-h;
    header.biPlanes=1;header.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);
    void* bytes=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bytes,nullptr,0);
    if(dc && bitmap) {
        auto old=SelectObject(dc,bitmap);
        SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);
        GdiFlush();
        result.width=w-2*inset;result.height=h-2*inset;
        if(result.width>0 && result.height>0) {
            result.pixels.resize(size_t(result.width)*result.height*4);
            for(int y=0;y<result.height;++y)
                std::memcpy(result.pixels.data()+size_t(y)*result.width*4,
                    static_cast<unsigned char*>(bytes)+(size_t(y+inset)*w+inset)*4,size_t(result.width)*4);
        }
        SelectObject(dc,old);
    }
    if(bitmap) DeleteObject(bitmap);
    if(dc) DeleteDC(dc);
    return result;
}
