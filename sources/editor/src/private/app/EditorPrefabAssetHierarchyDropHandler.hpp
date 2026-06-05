#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorPrefabAssetHierarchyDropHandler {
public:
    EditorPrefabAssetHierarchyDropHandler() = delete;

    [[nodiscard]] static bool Drop(HWND sourceWindow, HWND mainWindow, int x, int y, const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics, EditorSceneContext& sceneContext, const std::filesystem::path& assetPath, const std::filesystem::path& assetVirtualPath);
};

#endif

} // namespace kb::editor
