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
enum class EditorTransformToolMode : std::uint8_t;

#if defined(_WIN32)

struct SceneViewportToolbarRects {
    RECT toolbar{};
    RECT row{};
    RECT fpsCounter{};
    RECT realtimeButton{};
    RECT pipelineStats{};
    // Move / Rotate / Scale: the same tools W / E / R pick from the keyboard.
    RECT moveToolButton{};
    RECT rotateToolButton{};
    RECT scaleToolButton{};
    RECT renderProfileButton{};
    RECT gridToggleButton{};
    RECT gridStepButton{};
    RECT snapToggleButton{};
    RECT snapStepButton{};
    RECT rotationSnapButton{};
    RECT twoDButton{};
    // Shown and clickable only while a prefab is open in prefab edit mode.
    RECT prefabName{};
    RECT prefabSaveButton{};
    RECT prefabCloseButton{};
    RECT dropdownPanel{};
    std::array<RECT, 6U> dropdownItems{};
    RECT renderArea{};
};

// A value with a button on each side.
struct TerrainStepperRects {
    RECT minus{};
    RECT value{};
    RECT plus{};
};

struct TerrainViewportToolbarRects {
    RECT panel{};
    // The six tools, then what the active one can vary (Smooth/Flatten, the paint layer, the stamp blend).
    RECT sculptButton{};
    RECT smoothButton{};
    RECT paintButton{};
    RECT rampButton{};
    RECT stampButton{};
    RECT worldButton{};
    RECT variantButton{};
    TerrainStepperRects size{};
    TerrainStepperRects strength{};
    RECT slopeButton{};
    RECT advancedButton{};

    // The strip under the bar while the New World wizard or the Advanced settings are open.
    RECT drawer{};
    // New World: the four worlds, the character slider, Generate.
    std::array<RECT, 4U> worldPresetButtons{};
    RECT characterTrack{};
    RECT characterValue{};
    RECT generateButton{};
    // Advanced: the brush operation and tip menus, falloff / terrace step / seed, symmetry, resolution, the
    // stamp image, erosion.
    RECT operationButton{};
    RECT brushShapeButton{};
    TerrainStepperRects falloff{};
    TerrainStepperRects terraceStep{};
    TerrainStepperRects seed{};
    RECT symmetryXButton{};
    RECT symmetryZButton{};
    std::array<RECT, 5U> resolutionButtons{};
    RECT stampImageButton{};
    RECT thermalButton{};
    RECT hydraulicButton{};
    // The running job (a world, an erosion) in either drawer.
    RECT jobBar{};
    RECT cancelButton{};

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
    static void Paint(
        HDC dc,
        const RECT& content,
        const EditorTheme& theme,
        const EditorViewportPreviewState& state,
        EditorTransformToolMode toolMode,
        bool profilerWindowOpen = false);
    static void PaintPrefabEditBar(HDC dc, const RECT& content, const EditorTheme& theme, const EditorSceneContext& sceneContext);
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
