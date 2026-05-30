#include "rendering/GdiDrawing.hpp"

#include "rendering/gdi/GdiAlphaBlender.hpp"
#include "rendering/gdi/GdiColor.hpp"
#include "rendering/gdi/GdiFramePainter.hpp"
#include "rendering/gdi/GdiRect.hpp"
#include "rendering/gdi/GdiRectPainter.hpp"
#include "rendering/gdi/GdiTextPainter.hpp"

#if defined(_WIN32)

namespace kb::editor {

COLORREF GdiDrawing::ToColorRef(EditorColor color) {
    return GdiColor::ToColorRef(color);
}

RECT GdiDrawing::Inset(RECT rect, int amount) {
    return GdiRect::Inset(rect, amount);
}

RECT GdiDrawing::ToRect(const DockRect& rect) {
    return GdiRect::FromDockRect(rect);
}

void GdiDrawing::FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
    GdiRectPainter::Fill(dc, rect, color);
}

void GdiDrawing::FillRectAlpha(HDC target, const RECT& rect, COLORREF color, BYTE alpha) {
    GdiAlphaBlender::Fill(target, rect, color, alpha);
}

void GdiDrawing::DrawTextBlock(HDC dc, RECT rect, const char* text, COLORREF color) {
    GdiTextPainter::DrawBlock(dc, rect, text, color);
}

void GdiDrawing::DrawTabText(HDC dc, RECT rect, const char* text, COLORREF color) {
    GdiTextPainter::DrawTab(dc, rect, text, color);
}

void GdiDrawing::DrawCenteredText(HDC dc, RECT rect, const char* text, COLORREF color) {
    GdiTextPainter::DrawCentered(dc, rect, text, color);
}

void GdiDrawing::DrawSharpFrame(HDC dc, const RECT& rect, COLORREF fill, COLORREF border) {
    GdiFramePainter::DrawSharp(dc, rect, fill, border);
}

} // namespace kb::editor

#endif
