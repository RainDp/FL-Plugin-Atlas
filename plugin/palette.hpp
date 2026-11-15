#pragma once
#include <windows.h>

namespace palette {
// Pearl glass: one luminous accent, paper text, and a darker stage for artwork.
constexpr COLORREF background=RGB(16,17,22), input_surface=RGB(30,31,40);
constexpr COLORREF ink=RGB(246,245,250), muted=RGB(186,187,204), subdued=RGB(132,134,152);
constexpr COLORREF signal=RGB(206,192,248), focus=RGB(236,228,255), on_signal=RGB(36,28,52);
constexpr COLORREF face=RGB(44,45,56), hover=RGB(58,56,74), edge=RGB(96,94,116);
constexpr COLORREF selection=RGB(78,66,112), selection_edge=RGB(186,168,224);
constexpr COLORREF selected_text=RGB(236,230,250), preview_canvas=RGB(10,10,16);
}
