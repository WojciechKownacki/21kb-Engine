#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorDockController;
class EditorDockModel;
class EditorFloatingWindowManager;
class EditorHierarchySelectionController;
class EditorPlayModeState;
class EditorRenderBackendSettings;
class EditorSceneBgfxViewport;
class EditorSceneContext;
class EditorShellInteractionState;
struct EditorMetrics;
struct EditorPointerDragState;

class EditorWindowPointerHandler {
public:
#if defined(_WIN32)
    EditorWindowPointerHandler(
        HWND mainWindow,
        EditorDockModel& dockModel,
        EditorFloatingWindowManager& floatingWindows,
        EditorDockController& dockController,
        EditorHierarchySelectionController& hierarchySelection,
        EditorSceneContext& sceneContext,
        EditorRenderBackendSettings& renderBackendSettings,
        EditorSceneBgfxViewport& sceneViewport,
        EditorPlayModeState& playMode,
        EditorShellInteractionState& shellInteraction,
        EditorPointerDragState& pointerDrag,
        const EditorMetrics& metrics) noexcept;

    LRESULT HandleLeftButtonDown(HWND messageWindow, LPARAM lparam);
    LRESULT HandleLeftButtonDoubleClick(HWND messageWindow, LPARAM lparam);
    LRESULT HandleRightButtonDown(HWND messageWindow, LPARAM lparam);
    LRESULT HandleMiddleButtonDown(HWND messageWindow, LPARAM lparam);
    LRESULT HandleMouseMove(HWND messageWindow, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMouseWheel(HWND messageWindow, WPARAM wparam, LPARAM lparam);
    LRESULT HandleLeftButtonUp(HWND messageWindow, LPARAM lparam);
    LRESULT HandleRightButtonUp(HWND messageWindow, LPARAM lparam);
    LRESULT HandleMiddleButtonUp(HWND messageWindow);
    LRESULT HandleSetCursor(HWND messageWindow, WPARAM wparam, LPARAM lparam);
#endif

private:
#if defined(_WIN32)
    HWND mainWindow_ = nullptr;
    EditorDockModel& dockModel_;
    EditorFloatingWindowManager& floatingWindows_;
    EditorDockController& dockController_;
    EditorHierarchySelectionController& hierarchySelection_;
    EditorSceneContext& sceneContext_;
    EditorRenderBackendSettings& renderBackendSettings_;
    EditorSceneBgfxViewport& sceneViewport_;
    EditorPlayModeState& playMode_;
    EditorShellInteractionState& shellInteraction_;
    EditorPointerDragState& pointerDrag_;
    const EditorMetrics& metrics_;
#endif
};

} // namespace kb::editor
