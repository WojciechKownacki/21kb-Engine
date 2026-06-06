#pragma once

#include "engine/assets/AssetManager.hpp"
#include "kb/editor/assets/EditorAssetBrowserTypes.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

class EditorAssetBrowserAssetRows {
public:
    EditorAssetBrowserAssetRows() = delete;

    [[nodiscard]] static std::vector<EditorAssetItemRow> Build(
        const kb::assets::AssetManager& manager,
        const std::filesystem::path& selectedFolder,
        kb::assets::AssetId selectedAsset,
        bool recursive,
        std::string_view searchQuery,
        std::string_view typeFilter,
        bool showTemplates,
        EditorAssetSortMode sortMode);

    [[nodiscard]] static std::vector<std::string> AssetTypes(const kb::assets::AssetManager& manager);
};

} // namespace kb::editor
