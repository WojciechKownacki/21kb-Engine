#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
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
        const EditorMetrics& metrics) noexcept;

    LRESULT HandleLeftButtonDown(HWND messageWindow, LPARAM lparam);
    LRESULT HandleMouseMove(HWND messageWindow, LPARAM lparam);
    LRESULT HandleLeftButtonUp(HWND messageWindow);
    LRESULT HandleSetCursor(HWND messageWindow, WPARAM wparam, LPARAM lparam);
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] bool IsMainWindow(HWND candidate) const noexcept;

    HWND mainWindow_ = nullptr;
    EditorDockModel& dockModel_;
    EditorFloatingWindowManager& floatingWindows_;
    EditorDockController& dockController_;
    EditorHierarchySelectionController& hierarchySelection_;
    EditorSceneContext& sceneContext_;
    const EditorMetrics& metrics_;
#endif
};

} // namespace kb::editor
