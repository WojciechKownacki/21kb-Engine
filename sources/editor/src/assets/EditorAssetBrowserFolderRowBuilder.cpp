#include "assets/EditorAssetBrowserFolderRowBuilder.hpp"

#include "assets/EditorAssetBrowserPathUtils.hpp"

#include <algorithm>
#include <set>

namespace kb::editor {

std::vector<EditorAssetFolderRow> EditorAssetBrowserFolderRowBuilder::BuildTreeRows(
    const kb::assets::AssetManager& manager,
    const std::filesystem::path& selectedFolder,
    const std::unordered_set<std::string>& collapsedFolders) {
    const std::set<std::string> folders = asset_browser::AllFolderPaths(manager);

    std::vector<EditorAssetFolderRow> rows;
    rows.reserve(folders.size());
    for (const std::string& folder : folders) {
        if (asset_browser::IsHiddenByCollapsedAncestor(folder, collapsedFolders)) {
            continue;
        }
        const bool hasChildren = asset_browser::HasImmediateChild(folders, folder);
        rows.push_back(EditorAssetFolderRow{
            .virtualPath = std::filesystem::path{ folder },
            .name = asset_browser::FileNameOrRoot(folder),
            .depth = asset_browser::FolderDepth(folder),
            .selected = asset_browser::Normalize(selectedFolder) == folder,
            .hasChildren = hasChildren,
            .expanded = hasChildren && !collapsedFolders.contains(folder),
        });
    }
    return rows;
}

std::vector<EditorAssetFolderRow> EditorAssetBrowserFolderRowBuilder::BuildChildRows(
    const kb::assets::AssetManager& manager,
    const std::filesystem::path& selectedFolder,
    const std::filesystem::path& selectedContentFolder) {
    std::vector<EditorAssetFolderRow> rows;
    const std::string selected = asset_browser::Normalize(selectedFolder);
    const std::string selectedContent = asset_browser::Normalize(selectedContentFolder);
    for (const std::filesystem::path& folder : manager.VirtualFolders()) {
        const std::string normalized = asset_browser::Normalize(folder);
        if (normalized == selected || asset_browser::Normalize(asset_browser::ParentVirtualPath(folder)) != selected) {
            continue;
        }
        rows.push_back(EditorAssetFolderRow{
            .virtualPath = folder,
            .name = asset_browser::FileNameOrRoot(folder),
            .depth = 0,
            .selected = normalized == selectedContent,
            .hasChildren = false,
            .expanded = false,
        });
    }

    std::ranges::sort(rows, [](const EditorAssetFolderRow& left, const EditorAssetFolderRow& right) {
        return left.name < right.name;
    });
    return rows;
}

} // namespace kb::editor
