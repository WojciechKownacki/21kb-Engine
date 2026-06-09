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

class EditorSceneViewportGizmoInteraction {
public:
    EditorSceneViewportGizmoInteraction() = delete;

    [[nodiscard]] static bool BeginDrag(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);

    [[nodiscard]] static bool UpdateDragOrHover(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        bool leftButtonDown);

    [[nodiscard]] static bool TickActiveDrag(
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);

    [[nodiscard]] static bool EndDrag(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);

    [[nodiscard]] static bool EndDrag(EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static bool CancelDrag(EditorSceneContext& sceneContext) noexcept;

private:
    [[nodiscard]] static bool UpdateActiveDrag(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        bool leftButtonDown);

    [[nodiscard]] static bool UpdateHover(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        bool leftButtonDown);
};

#endif

} // namespace kb::editor
