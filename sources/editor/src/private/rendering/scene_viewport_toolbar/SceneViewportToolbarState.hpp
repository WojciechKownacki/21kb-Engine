#pragma once

#include "rendering/SceneViewportToolbarRenderer.hpp"

#include <cstdint>

namespace kb::editor {

#if defined(_WIN32)

enum class SceneViewportToolbarInfoHover : std::uint8_t {
    None,
    RenderStats,
    PipelineStats,
};

class SceneViewportToolbarState {
public:
    SceneViewportToolbarState() = delete;

    static void RecordPresentedFrame() noexcept;
    [[nodiscard]] static int CurrentPresentedFps() noexcept;
    static void RecordRenderStats(SceneViewportToolbarRenderStats stats) noexcept;
    [[nodiscard]] static SceneViewportToolbarRenderStats RenderStats() noexcept;
    [[nodiscard]] static SceneViewportToolbarInfoHover InfoHover() noexcept;
    [[nodiscard]] static bool UpdateInfoHover(const RECT& content, int x, int y) noexcept;
};

#endif

} // namespace kb::editor
