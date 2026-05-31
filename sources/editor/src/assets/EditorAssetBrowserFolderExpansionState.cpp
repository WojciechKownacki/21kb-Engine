#include "assets/EditorAssetBrowserFolderExpansionState.hpp"

#include "assets/EditorAssetBrowserPathUtils.hpp"

#include <set>

namespace kb::editor {

bool EditorAssetBrowserFolderExpansionState::ToggleFolderExpanded(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    const std::string normalized = asset_browser::Normalize(virtualPath);
    const std::set<std::string> folders = asset_browser::AllFolderPaths(manager);
    if (!folders.contains(normalized) || !asset_browser::HasImmediateChild(folders, normalized)) {
        return false;
    }

    if (collapsedFolders_.contains(normalized)) {
        collapsedFolders_.erase(normalized);
    } else {
        collapsedFolders_.insert(normalized);
    }
    return true;
}

void EditorAssetBrowserFolderExpansionState::ExpandAncestors(const std::filesystem::path& virtualPath) {
    std::filesystem::path parent = asset_browser::ParentVirtualPath(virtualPath);
    while (!parent.empty() && asset_browser::Normalize(parent) != asset_browser::Normalize(virtualPath)) {
        const std::string normalizedParent = asset_browser::Normalize(parent);
        collapsedFolders_.erase(normalizedParent);
        if (normalizedParent == "/Game") {
            break;
        }
        parent = asset_browser::ParentVirtualPath(parent);
    }
}

const std::unordered_set<std::string>& EditorAssetBrowserFolderExpansionState::CollapsedFolders() const noexcept {
    return collapsedFolders_;
}

} // namespace kb::editor
