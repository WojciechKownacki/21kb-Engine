#include "rendering/gdi/GdiTextPainter.hpp"

#if defined(_WIN32)
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

namespace kb::editor {
namespace {

void DrawWithUiFont(HDC dc, RECT rect, const char* text, COLORREF color, UINT format) {
    ScopedFont font(12, FW_NORMAL);
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, format | DT_NOPREFIX);
}

} // namespace

void GdiTextPainter::DrawBlock(HDC dc, RECT rect, const char* text, COLORREF color) {
    DrawWithUiFont(dc, rect, text, color, DT_LEFT | DT_TOP | DT_WORDBREAK);
}

void GdiTextPainter::DrawTab(HDC dc, RECT rect, const char* text, COLORREF color) {
    DrawWithUiFont(dc, rect, text, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void GdiTextPainter::DrawCentered(HDC dc, RECT rect, const char* text, COLORREF color) {
    DrawWithUiFont(dc, rect, text, color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

} // namespace kb::editor

#endif
