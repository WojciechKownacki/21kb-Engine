#pragma once

#include "docking/EditorDockController.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "platform/win32/EditorWindowClassRegistry.hpp"
#include "rendering/EditorGdiRenderer.hpp"
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
    EditorFloatingWindowManager floatingWindows;
    EditorDockController dockController;
    EditorHierarchySelectionController hierarchySelection;
    bool running = false;
#endif
};

} // namespace kb::editor
