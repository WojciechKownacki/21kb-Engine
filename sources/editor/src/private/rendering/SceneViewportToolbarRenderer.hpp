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

class EditorSceneContext;

#if defined(_WIN32)

struct SceneViewportToolbarRects {
    RECT toolbar{};
    RECT row{};
    RECT fpsCounter{};
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

struct TerrainViewportToolbarRects {
    RECT panel{};
    RECT selectButton{};
    RECT sculptButton{};
    RECT holesButton{};
    RECT paintButton{};
    RECT brushButton{};
    RECT brushShapeButton{};
    RECT sizeMinusButton{};
    RECT sizeValue{};
    RECT sizePlusButton{};
    RECT strengthMinusButton{};
    RECT strengthValue{};
    RECT strengthPlusButton{};
    RECT brushMenu{};
    std::array<RECT, 8U> brushItems{};
    RECT brushShapeMenu{};
    std::array<RECT, 6U> brushShapeItems{};
};

class SceneViewportToolbarRenderer {
public:
    SceneViewportToolbarRenderer() = delete;

    static constexpr int Height = 34;
    [[nodiscard]] static SceneViewportToolbarRects Resolve(const RECT& content) noexcept;
    [[nodiscard]] static SceneViewportToolbarRects Resolve(const RECT& content, const EditorViewportPreviewState& state) noexcept;
    [[nodiscard]] static SceneViewportToolbarRects Resolve(
        const RECT& content,
        const EditorViewportPreviewState& state,
        const EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static TerrainViewportToolbarRects ResolveTerrainTools(const RECT& content) noexcept;
    static void RecordFrameMilliseconds(double milliseconds) noexcept;
    static void Paint(HDC dc, const RECT& content, const EditorTheme& theme, const EditorViewportPreviewState& state);
    static void PaintTerrainTools(
        HDC dc,
        const RECT& content,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext);
    static void PaintTerrainPopup(
        HDC dc,
        const RECT& bounds,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        int hoveredItem = -1);
};

#endif

} // namespace kb::editor
