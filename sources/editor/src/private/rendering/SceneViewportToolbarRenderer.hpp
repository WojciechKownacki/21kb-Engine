#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct SceneViewportToolbarRects {
    RECT toolbar{};
    RECT row{};
    RECT fpsCounter{};
    RECT renderStats{};
    RECT ecsStats{};
    RECT pipelineStats{};
    RECT renderProfileButton{};
    RECT gridToggleButton{};
    RECT gridStepButton{};
    RECT snapToggleButton{};
    RECT snapStepButton{};
    RECT rotationSnapButton{};
    RECT dropdownPanel{};
    std::array<RECT, 6U> dropdownItems{};
    RECT renderArea{};
};

struct SceneViewportToolbarRenderStats {
    std::uint32_t submittedDrawCalls = 0;
    std::uint32_t submittedMeshes = 0;
    std::uint32_t gpuDispatches = 0;
    std::uint8_t msaaSamples = 0;
    bool gpuDrivenActive = false;
    bool postProcessActive = false;
    bool temporalAntiAliasingActive = false;
    bool bloomActive = false;
    bool finalCompositeActive = false;
};

struct SceneViewportToolbarEcsSystemStat {
    std::string name;
    std::uint64_t cpuTimeNanoseconds = 0;
    std::uint64_t jobsCount = 0;
    std::uint64_t entitiesProcessed = 0;
    std::uint64_t bytesTouched = 0;
};

struct SceneViewportToolbarEcsStats {
    std::uint64_t frameIndex = 0;
    std::uint64_t frameDurationNanoseconds = 0;
    std::uint64_t cpuTimeNanoseconds = 0;
    std::uint64_t jobsCount = 0;
    std::uint64_t entitiesProcessed = 0;
    std::uint64_t bytesTouched = 0;
    std::uint64_t systemCount = 0;
    std::uint64_t workerCount = 0;
    bool valid = false;
    std::vector<SceneViewportToolbarEcsSystemStat> topSystems;
};

class SceneViewportToolbarRenderer {
public:
    SceneViewportToolbarRenderer() = delete;

    static constexpr int Height = 34;

    [[nodiscard]] static SceneViewportToolbarRects Resolve(const RECT& content) noexcept;
    [[nodiscard]] static SceneViewportToolbarRects Resolve(const RECT& content, const EditorViewportPreviewState& state) noexcept;
    static void RecordPresentedFrame() noexcept;
    static void RecordRenderStats(SceneViewportToolbarRenderStats stats) noexcept;
    static void RecordEcsStats(SceneViewportToolbarEcsStats stats);
    [[nodiscard]] static bool UpdateInfoHover(const RECT& content, int x, int y) noexcept;
    static void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state);
};

#endif

} // namespace kb::editor
