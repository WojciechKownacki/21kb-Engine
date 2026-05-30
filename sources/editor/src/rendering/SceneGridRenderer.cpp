#include "rendering/SceneGridRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

namespace kb::editor {

void SceneGridRenderer::Paint(HDC dc, RECT scene, const EditorTheme& theme, const EditorMetrics& metrics) const {
    RECT sceneInner = GdiDrawing::Inset(scene, 20);
    sceneInner.top += metrics.tabStripHeight + 12;

    ScopedPen gridPen(1, GdiDrawing::ToColorRef(theme.gridLine));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, gridPen.handle));

    for (int x = sceneInner.left; x < sceneInner.right; x += 32) {
        MoveToEx(dc, x, sceneInner.top, nullptr);
        LineTo(dc, x, sceneInner.bottom);
    }

    for (int y = sceneInner.top; y < sceneInner.bottom; y += 32) {
        MoveToEx(dc, sceneInner.left, y, nullptr);
        LineTo(dc, sceneInner.right, y);
    }

    ScopedPen accentPen(2, GdiDrawing::ToColorRef(theme.accent));
    SelectObject(dc, accentPen.handle);

    const int centerX = (sceneInner.left + sceneInner.right) / 2;
    const int centerY = (sceneInner.top + sceneInner.bottom) / 2;
    Ellipse(dc, centerX - 48, centerY - 48, centerX + 48, centerY + 48);

    SelectObject(dc, oldPen);
}

} // namespace kb::editor

#endif
