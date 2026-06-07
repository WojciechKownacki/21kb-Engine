#pragma once

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
class EditorRightButtonDownRouter {
public:
    EditorRightButtonDownRouter(
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        EditorSceneContext& sceneContext,
        EditorPointerDragState& pointerDrag,
        const EditorMetrics& metrics) noexcept;

    void Handle(HWND messageWindow, int x, int y);

private:
    HWND mainWindow_ = nullptr;
    const EditorDockModel& dockModel_;
    const EditorFloatingWindowManager& floatingWindows_;
    EditorSceneContext& sceneContext_;
    EditorPointerDragState& pointerDrag_;
    const EditorMetrics& metrics_;
};
#endif

} // namespace kb::editor
