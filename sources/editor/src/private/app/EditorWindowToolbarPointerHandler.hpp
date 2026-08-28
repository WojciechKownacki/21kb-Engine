#pragma once

#include "app/EditorPlayModeState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorWindowToolbarPointerHandler {
public:
    EditorWindowToolbarPointerHandler() = delete;

#if defined(_WIN32)
    static bool HandleLeftButtonDown(
        HWND mainWindow,
        HWND messageWindow,
        int x,
        int y,
        EditorDockModel& dockModel,
        EditorFloatingWindowManager& floatingWindows,
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport,
        EditorRenderBackendSettings& renderBackendSettings,
        EditorPlayModeState& playMode,
        EditorShellInteractionState& shellInteraction,
        const EditorMetrics& metrics);

    static bool HandleMouseMove(
        HWND mainWindow,
        HWND messageWindow,
        int x,
        int y,
        EditorDockModel& dockModel,
        EditorShellInteractionState& shellInteraction,
        const EditorMetrics& metrics);

    static void ClearHover(EditorShellInteractionState& shellInteraction) noexcept;
#endif
};

} // namespace kb::editor
