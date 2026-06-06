#include "assets/EditorAssetBrowserState.hpp"

#include "assets/EditorAssetBrowserAssetRows.hpp"

namespace kb::editor {

std::vector<EditorAssetFolderRow> EditorAssetBrowserState::FolderRows(const kb::assets::AssetManager& manager) const {
    return selection_.FolderRows(manager);
}

std::vector<EditorAssetFolderRow> EditorAssetBrowserState::ChildFolderRows(const kb::assets::AssetManager& manager) const {
    if (!view_.ShowFolders()) {
        return {};
    }
    return selection_.ChildFolderRows(manager);
}

std::vector<EditorAssetItemRow> EditorAssetBrowserState::AssetRows(const kb::assets::AssetManager& manager) const {
    return EditorAssetBrowserAssetRows::Build(
        manager,
        selection_.SelectedFolder(),
        selection_.SelectedAsset(),
        view_.Recursive(),
        view_.SearchQuery(),
        view_.TypeFilter(),
        view_.ShowTemplates(),
        view_.SortMode());
}

std::vector<std::string> EditorAssetBrowserState::AssetTypes(const kb::assets::AssetManager& manager) const {
    return EditorAssetBrowserAssetRows::AssetTypes(manager);
}

} // namespace kb::editor
