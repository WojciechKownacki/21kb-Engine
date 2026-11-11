#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorGameView.hpp"
#include "scene/EditorSceneContext.hpp"
#include "scene/EditorViewportPreviewState.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

struct GameViewRects {
    RECT toolbar{};
    RECT resolutionButton{};
    RECT scaleLabel{};
    // The list of resolutions, in the panel's flow between the bar and the picture while it is open.
    RECT list{};
    std::array<RECT, EditorGameView::kResolutions.size()> items{};
    // Everything below the bar (and the list): the picture and the bars around it.
    RECT renderArea{};
};

// What the game renders at and where that appears inside the render area.
struct GameViewPicture {
    std::uint32_t renderWidth = 0U;
    std::uint32_t renderHeight = 0U;
    RECT destination{};
};

// The Game View: the game camera at the resolution the bar chooses, fitted inside the panel with bars
// where the shapes differ. It shows no editor overlay and no gizmo.
class GamePanelContentRenderer {
public:
    static constexpr int ToolbarHeight = 34;

    [[nodiscard]] static GameViewRects Resolve(const RECT& content, bool listOpen) noexcept;
    [[nodiscard]] static bool ListOpen(const EditorSceneContext& sceneContext, std::uint64_t panelId) noexcept;
    // The index of the list entry under the point, when the list is open.
    [[nodiscard]] static std::optional<std::size_t> ItemAt(const GameViewRects& rects, int x, int y) noexcept;
    // True while a game camera is there to be shown: the game is running and has a primary camera.
    [[nodiscard]] static bool ShowsGame(const EditorSceneContext& sceneContext) noexcept;
    // The chosen resolution as the viewport state the renderer reads, so the Scene View and the Game View
    // size their pictures with the same code.
    [[nodiscard]] static EditorViewportPreviewState PictureState(const EditorSceneContext& sceneContext);
    [[nodiscard]] static GameViewPicture ResolvePicture(const RECT& renderArea, const EditorSceneContext& sceneContext);

    [[nodiscard]] static EditorSceneBgfxViewport::PresentSettings BuildSettings(
        const RECT& content, const DockPanel& panel, const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings);
    // False when there is nothing to show; the viewport then stays hidden.
    [[nodiscard]] static bool PresentViewport(
        EditorSceneBgfxViewport& sceneViewport,
        HWND sceneViewportHost,
        const RECT& content,
        const DockPanel& panel,
        const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings);

    void Paint(
        HDC dc,
        const RECT& content,
        const DockPanel& panel,
        const EditorTheme& theme,
        const EditorSceneContext& sceneContext,
        const EditorRenderBackendSettings& renderBackendSettings,
        EditorSceneBgfxViewport* sceneViewport,
        HWND sceneViewportHost) const;
};

#endif

} // namespace kb::editor
