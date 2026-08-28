#include "rendering/scene_viewport_toolbar/SceneViewportToolbarInfoRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLabelFormat.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarMetrics.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarState.hpp"

#include <array>
#include <span>
#include <string_view>

namespace kb::editor {

void SceneViewportToolbarInfoRenderer::PaintFpsCounter(HDC dc, RECT rect, const EditorTheme& theme) {
    const COLORREF fill = SceneViewportToolbarDrawing::Blend(SceneViewportToolbarDrawing::ToolbarRowColor(theme), RGB(0, 0, 0), 1, 5);
    const COLORREF border = SceneViewportToolbarDrawing::Blend(GdiDrawing::ToColorRef(theme.borderPanel), RGB(96, 109, 132), 1, 8);
    SceneViewportToolbarDrawing::FillRound(dc, rect, fill, border, SceneViewportToolbarMetrics::ButtonRadius);

    std::array<char, 16> text{};
    const SceneViewportToolbarState::FrameRateReading reading = SceneViewportToolbarState::CurrentReading();
    const std::string_view label = SceneViewportToolbarLabelFormat::Fps(
        std::span<char>{ text }, reading.fps, reading.live);

    ScopedFont font(11, FW_SEMIBOLD);
    const ScopedGdiObject selectedFont(dc, font.handle);
    const int previousBkMode = SetBkMode(dc, TRANSPARENT);
    // A held reading is dimmed as well as relabelled: the chip should not look like it is
    // still counting when it is not.
    const COLORREF previousTextColor = SetTextColor(
        dc,
        GdiDrawing::ToColorRef(reading.live ? theme.textSecondary : theme.textDisabled));
    DrawTextA(dc, label.data(), static_cast<int>(label.size()), &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetTextColor(dc, previousTextColor);
    SetBkMode(dc, previousBkMode);
}

} // namespace kb::editor

#endif
