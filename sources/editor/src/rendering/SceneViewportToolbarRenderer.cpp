#include "rendering/SceneViewportToolbarRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"

#include <cstdio>

namespace kb::editor {
namespace {

[[nodiscard]] RECT ButtonRect(RECT toolbar, int left, int width) noexcept {
    RECT rect = toolbar;
    rect.left += left;
    rect.right = rect.left + width;
    rect.top += 4;
    rect.bottom -= 4;
    return rect;
}

void DrawButton(HDC dc, RECT rect, const char* text, const EditorTheme& theme) {
    GdiDrawing::DrawSharpFrame(dc, rect, GdiDrawing::ToColorRef(theme.toolbarButton), GdiDrawing::ToColorRef(theme.borderPanel));
    GdiDrawing::DrawCenteredText(dc, rect, text, GdiDrawing::ToColorRef(theme.textPrimary));
}

} // namespace

SceneViewportToolbarRects SceneViewportToolbarRenderer::Resolve(const RECT& content) noexcept {
    SceneViewportToolbarRects rects{};
    rects.toolbar = content;
    rects.toolbar.bottom = rects.toolbar.top + Height;
    rects.profileButton = ButtonRect(rects.toolbar, 8, 104);
    rects.fitButton = ButtonRect(rects.toolbar, 118, 64);
    rects.cameraButton = ButtonRect(rects.toolbar, 188, 112);
    rects.renderArea = content;
    rects.renderArea.top = rects.toolbar.bottom;
    return rects;
}

void SceneViewportToolbarRenderer::Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state) {
    const SceneViewportToolbarRects rects = Resolve(content);
    GdiDrawing::FillRectColor(dc, rects.toolbar, GdiDrawing::ToColorRef(theme.toolbar));

    const EditorViewportProfile profile = state.Profile();
    DrawButton(dc, rects.profileButton, profile.label.data(), theme);
    DrawButton(dc, rects.fitButton, EditorViewportFitModeLabel(state.FitMode()), theme);
    DrawButton(dc, rects.cameraButton, EditorViewportCameraModeLabel(state.CameraMode()), theme);

    char resolution[96]{};
    if (profile.width == 0U || profile.height == 0U) {
        std::snprintf(resolution, sizeof(resolution), "Render: panel");
    } else {
        std::snprintf(resolution, sizeof(resolution), "Render: %ux%u", profile.width, profile.height);
    }
    RECT textRect = rects.toolbar;
    textRect.left = rects.cameraButton.right + 12;
    textRect.right -= 8;
    GdiDrawing::DrawTabText(dc, textRect, resolution, GdiDrawing::ToColorRef(theme.textDisabled));
}

} // namespace kb::editor

#endif
