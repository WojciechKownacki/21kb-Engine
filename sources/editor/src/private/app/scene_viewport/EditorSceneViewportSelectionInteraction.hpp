#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

struct EditorMetrics;

#if defined(_WIN32)

class EditorSceneViewportSelectionInteraction {
public:
    EditorSceneViewportSelectionInteraction() = delete;

    [[nodiscard]] static bool SelectAt(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);
};

#endif

} // namespace kb::editor
