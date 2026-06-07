#pragma once

#include "app/EditorPointerDragState.hpp"
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

class EditorSceneViewportMeshDragPreview {
public:
    EditorSceneViewportMeshDragPreview() = delete;

    [[nodiscard]] static bool Update(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        EditorPointerDragState& drag);

    [[nodiscard]] static bool Commit(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        EditorPointerDragState& drag);

    static void Cancel(EditorSceneContext& sceneContext, EditorPointerDragState& drag) noexcept;
};

#endif

} // namespace kb::editor
