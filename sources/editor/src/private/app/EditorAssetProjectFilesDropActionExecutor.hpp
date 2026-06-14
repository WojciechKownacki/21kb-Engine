#pragma once

#include "engine/assets/AssetId.hpp"
#include "scene/EditorSceneContext.hpp"

#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

class EditorAssetProjectFilesDropActionExecutor {
public:
    EditorAssetProjectFilesDropActionExecutor() = delete;

    [[nodiscard]] static bool ExecuteAssetDropMenu(
        HWND sourceWindow,
        int x,
        int y,
        EditorSceneContext& sceneContext,
        kb::assets::AssetId assetId,
        const std::filesystem::path& targetFolder);

    [[nodiscard]] static bool ExecuteFolderDropMenu(
        HWND sourceWindow,
        int x,
        int y,
        EditorSceneContext& sceneContext,
        const std::filesystem::path& sourceVirtualFolder,
        const std::filesystem::path& targetFolder);
};

#endif

} // namespace kb::editor
