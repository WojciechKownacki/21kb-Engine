#include "rendering/DockDropPreviewRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void DockDropPreviewRenderer::Paint(HDC dc, const DockDropPreview& preview, const EditorTheme& theme) const {
    if (preview.rect.Empty()) {
        return;
    }

    RECT previewRect = GdiDrawing::ToRect(preview.rect);
    if (preview.kind == DockDropPreviewKind::StripMarker) {
        GdiDrawing::FillRectColor(dc, previewRect, GdiDrawing::ToColorRef(theme.accent));
        return;
    }

    GdiDrawing::FillRectAlpha(dc, previewRect, RGB(64, 102, 146), 112);
    ScopedPen pen(2, GdiDrawing::ToColorRef(theme.accent));
    HPEN oldPenPreview = static_cast<HPEN>(SelectObject(dc, pen.handle));
    HBRUSH oldBrushPreview = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, previewRect.left + 1, previewRect.top + 1, previewRect.right - 1, previewRect.bottom - 1);
    SelectObject(dc, oldBrushPreview);
    SelectObject(dc, oldPenPreview);
}

} // namespace kb::editor

#endif
