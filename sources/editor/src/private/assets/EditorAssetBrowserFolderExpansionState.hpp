#pragma once

#include "engine/assets/AssetManager.hpp"

#include <filesystem>
#include <string>
#include <unordered_set>

namespace kb::editor {

class EditorAssetBrowserFolderExpansionState {
public:
    [[nodiscard]] bool ToggleFolderExpanded(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager);
    void ExpandAncestors(const std::filesystem::path& virtualPath);

    [[nodiscard]] const std::unordered_set<std::string>& CollapsedFolders() const noexcept;

private:
    std::unordered_set<std::string> collapsedFolders_;
};

} // namespace kb::editor
