#include "rendering/scene_viewport_toolbar/SceneViewportToolbarInfoRenderer.hpp"

#if defined(_WIN32)
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarLabelFormat.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarState.hpp"

#include <array>
#include <span>
#include <string_view>

namespace kb::editor {

void SceneViewportToolbarInfoRenderer::PaintFpsCounter(HDC dc, RECT rect, const EditorTheme& theme, bool profilerWindowOpen) {
    std::array<char, 16> text{};
    const SceneViewportToolbarState::FrameRateReading reading = SceneViewportToolbarState::CurrentReading();
    const std::string_view label = SceneViewportToolbarLabelFormat::Fps(
        std::span<char>{ text }, reading.fps, reading.live);
    // The chip opens the window that chooses the counters, so it looks like the toolbar's other
    // value buttons; a held reading is dimmed as well as relabelled, so it does not look as if it
    // is still counting.
    SceneViewportToolbarDrawing::DrawValueButton(
        dc, rect, HeroIconKind::AdjustmentsHorizontal, label, theme, profilerWindowOpen, !reading.live);
}

} // namespace kb::editor

#endif
