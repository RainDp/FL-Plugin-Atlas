#pragma once
#include <windows.h>

namespace palette {
// Studio: opaque graphite surfaces, warm paper text and one amber action color.
constexpr COLORREF background=RGB(24,26,28), sidebar=RGB(20,22,24), inspector=RGB(28,30,32);
constexpr COLORREF input_surface=RGB(33,36,39), footer=RGB(20,22,24);
constexpr COLORREF ink=RGB(239,237,231), muted=RGB(169,173,175), subdued=RGB(134,141,146);
constexpr COLORREF signal=RGB(227,183,99), focus=RGB(245,204,127), on_signal=RGB(30,25,17);
constexpr COLORREF face=RGB(40,44,47), hover=RGB(49,54,57), edge=RGB(70,76,80);
constexpr COLORREF divider=RGB(47,51,54), selection=RGB(56,47,33), selection_edge=RGB(118,96,56);
constexpr COLORREF selected_text=RGB(237,208,157), preview_canvas=RGB(17,19,21);
constexpr COLORREF instrument=RGB(139,187,181), instrument_surface=RGB(34,49,49);
}
