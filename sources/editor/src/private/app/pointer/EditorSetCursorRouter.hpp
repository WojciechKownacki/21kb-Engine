#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "app/EditorPointerDragState.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)
class EditorSetCursorRouter {
public:
    EditorSetCursorRouter(
        HWND mainWindow,
        EditorDockModel& dockModel,
        EditorFloatingWindowManager& floatingWindows,
        EditorDockController& dockController,
        EditorSceneContext& sceneContext,
        EditorPointerDragState& pointerDrag,
        const EditorMetrics& metrics) noexcept;

    [[nodiscard]] LRESULT Handle(HWND messageWindow, WPARAM wparam, LPARAM lparam);

private:
    HWND mainWindow_ = nullptr;
    EditorDockModel& dockModel_;
    EditorFloatingWindowManager& floatingWindows_;
    EditorDockController& dockController_;
    EditorSceneContext& sceneContext_;
    EditorPointerDragState& pointerDrag_;
    const EditorMetrics& metrics_;
};
#endif

} // namespace kb::editor
