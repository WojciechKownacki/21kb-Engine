#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "app/EditorPlayModeState.hpp"
#include "app/EditorPointerDragState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "rendering/EditorGdiRenderer.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
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
    EditorRenderBackendSettings& renderBackendSettings;
    EditorSceneBgfxViewport& sceneViewport;
    EditorFloatingWindowManager& floatingWindows;
    EditorDockController& dockController;
    EditorHierarchySelectionController& hierarchySelection;
    EditorPlayModeState& playMode;
    EditorShellInteractionState& shellInteraction;
    EditorPointerDragState& pointerDrag;
#endif
};

} // namespace kb::editor
