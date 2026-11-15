#pragma once
#include <windows.h>

namespace layout {
constexpr int scale_percent=80;
inline int scale(int value) { return MulDiv(value,scale_percent,100); }
inline void scale_rect(RECT& r) {
    r={scale(r.left),scale(r.top),scale(r.right),scale(r.bottom)};
}
}
