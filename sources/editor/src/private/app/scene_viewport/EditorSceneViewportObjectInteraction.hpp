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

#if defined(_WIN32)

class EditorSceneViewportObjectInteraction {
public:
    EditorSceneViewportObjectInteraction() = delete;

    [[nodiscard]] static bool UpdateMeshDragPreview(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        EditorPointerDragState& drag);

    [[nodiscard]] static bool CommitMeshDragPreview(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        EditorPointerDragState& drag);

    static void CancelMeshDragPreview(EditorSceneContext& sceneContext, EditorPointerDragState& drag) noexcept;

    [[nodiscard]] static bool BeginGizmoDrag(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);

    [[nodiscard]] static bool UpdateGizmoDragOrHover(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext,
        bool leftButtonDown);

    [[nodiscard]] static bool TickGizmoDrag(
        HWND mainWindow,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);

    [[nodiscard]] static bool EndGizmoDrag(
        HWND sourceWindow,
        HWND mainWindow,
        int x,
        int y,
        const EditorDockModel& dockModel,
        const EditorFloatingWindowManager& floatingWindows,
        const EditorMetrics& metrics,
        EditorSceneContext& sceneContext);

    [[nodiscard]] static bool EndGizmoDrag(EditorSceneContext& sceneContext) noexcept;
    [[nodiscard]] static bool CancelGizmoDrag(EditorSceneContext& sceneContext) noexcept;

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
