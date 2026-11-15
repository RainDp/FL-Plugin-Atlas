#pragma once
#include <windows.h>

// A single cached opaque background shared by the native child controls.
class StudioSurface {
public:
    ~StudioSurface() { clear(); }
    void build(HWND dialog);
    void clear();
    HBRUSH brush_for(HDC dc, HWND child, HWND dialog) const;
    void paint(HDC dc, HWND child, HWND dialog, RECT region) const;
private:
    HBRUSH pattern=nullptr;
};
