#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#include <span>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorHierarchyEntityHierarchyDropHandler {
public:
    EditorHierarchyEntityHierarchyDropHandler() = delete;

    [[nodiscard]] static bool Drop(HWND sourceWindow, HWND mainWindow, int x, int y, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, EditorSceneContext& sceneContext, std::span<const kb::scene::SceneEntity> entities);
};

#endif

} // namespace kb::editor
