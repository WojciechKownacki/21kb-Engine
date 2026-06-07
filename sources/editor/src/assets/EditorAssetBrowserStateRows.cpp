#include "assets/EditorAssetBrowserState.hpp"

#include "assets/EditorAssetBrowserAssetRows.hpp"

namespace kb::editor {
namespace {

[[nodiscard]] std::string AssetDeleteTargetKey(kb::assets::AssetId id) {
    return "Asset:" + std::to_string(id.value);
}

[[nodiscard]] std::string FolderDeleteTargetKey(const std::filesystem::path& virtualPath) {
    return "Folder:" + kb::assets::NormalizeAssetPath(virtualPath);
}

} // namespace

std::vector<EditorAssetFolderRow> EditorAssetBrowserState::FolderRows(const kb::assets::AssetManager& manager) const {
    return selection_.FolderRows(manager);
}

std::vector<EditorAssetFolderRow> EditorAssetBrowserState::ChildFolderRows(const kb::assets::AssetManager& manager) const {
    if (!view_.ShowFolders()) {
        return {};
    }
    std::vector<EditorAssetFolderRow> rows = selection_.ChildFolderRows(manager);
    for (EditorAssetFolderRow& row : rows) {
        row.selected = selection_.IsContentFolderSelected(row.virtualPath);
    }
    return rows;
}

std::vector<EditorAssetItemRow> EditorAssetBrowserState::AssetRows(const kb::assets::AssetManager& manager) const {
    std::vector<EditorAssetItemRow> rows = EditorAssetBrowserAssetRows::Build(
        manager,
        selection_.SelectedFolder(),
        selection_.SelectedAsset(),
        view_.Recursive(),
        view_.SearchQuery(),
        view_.TypeFilter(),
        view_.ShowTemplates(),
        view_.SortMode());
    for (EditorAssetItemRow& row : rows) {
        row.selected = selection_.IsAssetSelected(row.metadata.id);
    }
    return rows;
}

std::vector<EditorAssetSelectionSummaryRow> EditorAssetBrowserState::SelectedContentRows(const kb::assets::AssetManager& manager) const {
    std::vector<EditorAssetSelectionSummaryRow> rows;
    const std::vector<EditorAssetFolderRow> folders = ChildFolderRows(manager);
    const std::vector<EditorAssetItemRow> assets = AssetRows(manager);
    rows.reserve(folders.size() + assets.size());
    for (const EditorAssetFolderRow& folder : folders) {
        if (!folder.selected) {
            continue;
        }
        rows.push_back(EditorAssetSelectionSummaryRow{
            .key = FolderDeleteTargetKey(folder.virtualPath),
            .id = kb::assets::NormalizeAssetPath(folder.virtualPath),
            .name = folder.name,
            .objectType = "Folder",
            .checked = IsDeleteTargetChecked(FolderDeleteTargetKey(folder.virtualPath)),
        });
    }
    for (const EditorAssetItemRow& asset : assets) {
        if (!asset.selected) {
            continue;
        }
        rows.push_back(EditorAssetSelectionSummaryRow{
            .key = AssetDeleteTargetKey(asset.metadata.id),
            .id = std::to_string(asset.metadata.id.value),
            .name = asset.metadata.name.empty() ? asset.metadata.virtualPath.filename().string() : asset.metadata.name,
            .objectType = asset.metadata.type.empty() ? "Asset" : asset.metadata.type,
            .checked = IsDeleteTargetChecked(AssetDeleteTargetKey(asset.metadata.id)),
        });
    }
    return rows;
}

std::vector<EditorAssetSelectionSummaryRow> EditorAssetBrowserState::DeleteTargetRows(const kb::assets::AssetManager& manager) const {
    std::vector<EditorAssetSelectionSummaryRow> rows = SelectedContentRows(manager);
    if (!rows.empty()) {
        return rows;
    }

    if (SelectedAsset().IsValid()) {
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().Find(SelectedAsset()); metadata != nullptr) {
            rows.push_back(EditorAssetSelectionSummaryRow{
                .key = AssetDeleteTargetKey(metadata->id),
                .id = std::to_string(metadata->id.value),
                .name = metadata->name.empty() ? metadata->virtualPath.filename().string() : metadata->name,
                .objectType = metadata->type.empty() ? "Asset" : metadata->type,
                .checked = IsDeleteTargetChecked(AssetDeleteTargetKey(metadata->id)),
            });
        }
        return rows;
    }

    if (SelectionKind() == EditorAssetBrowserSelectionKind::Folder) {
        const std::filesystem::path folder = SelectedContentFolder().empty() ? SelectedFolder() : SelectedContentFolder();
        if (!folder.empty()) {
            rows.push_back(EditorAssetSelectionSummaryRow{
                .key = FolderDeleteTargetKey(folder),
                .id = kb::assets::NormalizeAssetPath(folder),
                .name = folder.filename().string().empty() ? kb::assets::NormalizeAssetPath(folder) : folder.filename().string(),
                .objectType = "Folder",
                .checked = IsDeleteTargetChecked(FolderDeleteTargetKey(folder)),
            });
        }
    }
    return rows;
}

std::vector<EditorAssetSelectionSummaryRow> EditorAssetBrowserState::CheckedDeleteTargetRows(const kb::assets::AssetManager& manager) const {
    std::vector<EditorAssetSelectionSummaryRow> checkedRows;
    for (const EditorAssetSelectionSummaryRow& row : DeleteTargetRows(manager)) {
        if (row.checked) {
            checkedRows.push_back(row);
        }
    }
    return checkedRows;
}

std::vector<std::string> EditorAssetBrowserState::AssetTypes(const kb::assets::AssetManager& manager) const {
    return EditorAssetBrowserAssetRows::AssetTypes(manager);
}

} // namespace kb::editor
