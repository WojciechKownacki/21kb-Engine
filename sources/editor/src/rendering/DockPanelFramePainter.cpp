#include "rendering/DockPanelFramePainter.hpp"

#if defined(_WIN32)
#include "rendering/EditorSurfacePainter.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/GdiResources.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

namespace kb::editor {

void DockPanelFramePainter::Paint(HDC dc, const RECT& rect, DockPanelKind kind, const EditorTheme& theme) const {
    if (kind == DockPanelKind::Scene) {
        PaintTransparentContentFrame(dc, rect, theme);
        return;
    }

    EditorSurfacePainter::Frame(dc, rect, theme, EditorSurfaceKind::DockPanel, GdiDrawing::ToColorRef(theme.borderPanel));
}

void DockPanelFramePainter::PaintTransparentContentFrame(HDC dc, const RECT& rect, const EditorTheme& theme) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }

    ScopedPen borderPen(1, GdiDrawing::ToColorRef(theme.borderPanel));
    const ScopedGdiObject selectedPen(dc, borderPen.handle);
    const ScopedGdiObject selectedBrush(dc, GetStockObject(NULL_BRUSH));

    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
}

} // namespace kb::editor

#endif
