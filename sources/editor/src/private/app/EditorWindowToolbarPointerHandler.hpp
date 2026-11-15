#pragma once

#include "app/EditorPlayModeState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorMenuTable.hpp"
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
        const EditorMetrics& metrics,
        bool pointerDown = false);

    static void ClearHover(EditorShellInteractionState& shellInteraction) noexcept;

    // Lets a check see which table row a click chose. The observer runs for every enabled row that is
    // clicked; when it returns true it has taken the row's place, so nothing it would do (a dialog, a
    // browser, a file operation) runs. Null puts the usual behaviour back.
    using MenuRowObserver = bool (*)(const EditorMenuRowView& row);
    static void SetMenuRowObserver(MenuRowObserver observer) noexcept;
#endif
};

} // namespace kb::editor
