#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "app/EditorPointerDragState.hpp"
#include "rendering/EditorGdiRenderer.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorHierarchySelectionController.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

struct EditorWindowMessageContext {
#if defined(_WIN32)
    HWND& mainWindow;
    bool& running;
    EditorDockModel& dockModel;
    EditorSceneContext& sceneContext;
    EditorTheme& theme;
    EditorMetrics& metrics;
    EditorGdiRenderer& renderer;
    EditorSceneBgfxViewport& sceneViewport;
    EditorFloatingWindowManager& floatingWindows;
    EditorDockController& dockController;
    EditorHierarchySelectionController& hierarchySelection;
    EditorPointerDragState& pointerDrag;
#endif
};

} // namespace kb::editor
