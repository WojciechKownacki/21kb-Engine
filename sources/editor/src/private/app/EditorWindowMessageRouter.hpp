#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "rendering/EditorGdiRenderer.hpp"
#include "scene/EditorHierarchySelectionController.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorWindowMessageRouter {
public:
#if defined(_WIN32)
    EditorWindowMessageRouter(
        HWND& mainWindow,
        bool& running,
        EditorDockModel& dockModel,
        EditorSceneContext& sceneContext,
        EditorTheme& theme,
        EditorMetrics& metrics,
        EditorGdiRenderer& renderer,
        EditorFloatingWindowManager& floatingWindows,
        EditorDockController& dockController,
        EditorHierarchySelectionController& hierarchySelection) noexcept;

    [[nodiscard]] LRESULT Handle(HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam);
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] bool IsMainWindow(HWND candidate) const noexcept;
    void Paint(HWND paintWindow) const;

    HWND& mainWindow_;
    bool& running_;
    EditorDockModel& dockModel_;
    EditorSceneContext& sceneContext_;
    EditorTheme& theme_;
    EditorMetrics& metrics_;
    EditorGdiRenderer& renderer_;
    EditorFloatingWindowManager& floatingWindows_;
    EditorDockController& dockController_;
    EditorHierarchySelectionController& hierarchySelection_;
#endif
};

} // namespace kb::editor
