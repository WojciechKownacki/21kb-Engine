#pragma once

#include "engine/assets/AssetManager.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace kb::editor {

class EditorAssetBrowserFolderRowBuilder {
public:
    EditorAssetBrowserFolderRowBuilder() = delete;

    [[nodiscard]] static std::vector<EditorAssetFolderRow> BuildTreeRows(
        const kb::assets::AssetManager& manager,
        const std::filesystem::path& selectedFolder,
        const std::unordered_set<std::string>& collapsedFolders);

    [[nodiscard]] static std::vector<EditorAssetFolderRow> BuildChildRows(
        const kb::assets::AssetManager& manager,
        const std::filesystem::path& selectedFolder,
        const std::filesystem::path& selectedContentFolder);
};

} // namespace kb::editor
