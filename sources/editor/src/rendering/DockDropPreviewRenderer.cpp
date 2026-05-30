#include "rendering/DockDropPreviewRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

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
    const ScopedGdiObject selectedPen(dc, pen.handle);
    const ScopedGdiObject selectedBrush(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, previewRect.left + 1, previewRect.top + 1, previewRect.right - 1, previewRect.bottom - 1);
}

} // namespace kb::editor

#endif
