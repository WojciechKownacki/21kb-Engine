#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "app/EditorPointerDragState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)
class EditorMouseMoveRouter {
public:
    EditorMouseMoveRouter(
        HWND mainWindow,
        EditorDockModel& dockModel,
        EditorFloatingWindowManager& floatingWindows,
        EditorDockController& dockController,
        EditorSceneContext& sceneContext,
        EditorSceneBgfxViewport& sceneViewport,
        EditorShellInteractionState& shellInteraction,
        EditorPointerDragState& pointerDrag,
        const EditorMetrics& metrics) noexcept;

    void Handle(HWND messageWindow, int x, int y, bool leftButtonDown, bool rightButtonDown);

private:
    HWND mainWindow_ = nullptr;
    EditorDockModel& dockModel_;
    EditorFloatingWindowManager& floatingWindows_;
    EditorDockController& dockController_;
    EditorSceneContext& sceneContext_;
    EditorSceneBgfxViewport& sceneViewport_;
    EditorShellInteractionState& shellInteraction_;
    EditorPointerDragState& pointerDrag_;
    const EditorMetrics& metrics_;
};
#endif

} // namespace kb::editor
