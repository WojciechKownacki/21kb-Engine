#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "app/EditorPointerDragState.hpp"
#include "scene/EditorHierarchySelectionController.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

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
        EditorPointerDragState& pointerDrag,
        const EditorMetrics& metrics) noexcept;

    LRESULT HandleLeftButtonDown(HWND messageWindow, LPARAM lparam);
    LRESULT HandleMouseMove(HWND messageWindow, LPARAM lparam);
    LRESULT HandleLeftButtonUp(HWND messageWindow, LPARAM lparam);
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
    EditorPointerDragState& pointerDrag_;
    const EditorMetrics& metrics_;
#endif
};

} // namespace kb::editor
