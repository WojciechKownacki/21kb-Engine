#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorHierarchySelectionController {
public:
#if defined(_WIN32)
    [[nodiscard]] bool HandlePointerDown(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext) const;
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] static bool SelectAtContentPoint(const RECT& content, int x, int y, EditorSceneContext& sceneContext);
    [[nodiscard]] static DockLayout BuildMainLayout(HWND mainWindow, const EditorDockModel& dockModel, const EditorMetrics& metrics);
#endif
};

} // namespace kb::editor
