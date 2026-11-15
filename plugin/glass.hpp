#pragma once
#include <windows.h>

// An opaque, cached optical backdrop for a child window embedded in a DAW.
class GlassSurface {
public:
    ~GlassSurface() { clear(); }
    void build(HWND dialog);
    void clear();
    HBRUSH brush_for(HDC dc, HWND child, HWND dialog) const;
    void paint(HDC dc, HWND child, HWND dialog, RECT region) const;
private:
    HBRUSH pattern=nullptr;
};
