#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "app/EditorPlayModeState.hpp"
#include "app/EditorPointerDragState.hpp"
#include "app/EditorShellInteractionState.hpp"
#include "platform/win32/EditorWindowClassRegistry.hpp"
#include "platform/win32/Win32InputCollector.hpp"
#include "platform/win32/Win32InputDebugLogger.hpp"
#include "rendering/EditorGdiRenderer.hpp"
#include "rendering/EditorRenderBackendSettings.hpp"
#include "rendering/EditorSceneBgfxViewport.hpp"
#include "scene/EditorHierarchySelectionController.hpp"
#include "scene/EditorSceneContext.hpp"

#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

struct EditorApplicationState {
#if defined(_WIN32)
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    EditorWindowClassRegistry windowClasses;
    EditorDockModel dockModel;
    EditorSceneContext sceneContext;
    EditorTheme theme = MakeEditorDarkTheme();
    EditorMetrics metrics;
    EditorGdiRenderer renderer;
    EditorRenderBackendSettings renderBackendSettings;
    EditorSceneBgfxViewport sceneViewport;
    EditorFloatingWindowManager floatingWindows;
    EditorDockController dockController;
    EditorHierarchySelectionController hierarchySelection;
    EditorPlayModeState playMode;
    EditorShellInteractionState shellInteraction;
    EditorPointerDragState pointerDrag;
    Win32InputCollector inputCollector;
    Win32InputDebugLogger inputDebugLogger;
    bool running = false;
#endif
};

} // namespace kb::editor
