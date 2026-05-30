#include "rendering/gdi/GdiTextPainter.hpp"

#if defined(_WIN32)

namespace kb::editor {

void GdiTextPainter::DrawBlock(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK);
}

void GdiTextPainter::DrawTab(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void GdiTextPainter::DrawCentered(HDC dc, RECT rect, const char* text, COLORREF color) {
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

} // namespace kb::editor

#endif
