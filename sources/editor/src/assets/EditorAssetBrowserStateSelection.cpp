#include "assets/EditorAssetBrowserState.hpp"

#include <algorithm>
#include <vector>

namespace kb::editor {
namespace {

struct ContentSelectionItem {
    EditorAssetBrowserSelectionKind kind = EditorAssetBrowserSelectionKind::None;
    std::filesystem::path folder;
    kb::assets::AssetId asset{};
};

[[nodiscard]] std::vector<ContentSelectionItem> VisibleContentItems(
    const EditorAssetBrowserState& state,
    const kb::assets::AssetManager& manager) {
    std::vector<ContentSelectionItem> items;
    const std::vector<EditorAssetFolderRow> folders = state.ChildFolderRows(manager);
    const std::vector<EditorAssetItemRow> assets = state.AssetRows(manager);
    items.reserve(folders.size() + assets.size());
    for (const EditorAssetFolderRow& folder : folders) {
        items.push_back(ContentSelectionItem{
            .kind = EditorAssetBrowserSelectionKind::Folder,
            .folder = folder.virtualPath,
        });
    }
    for (const EditorAssetItemRow& asset : assets) {
        items.push_back(ContentSelectionItem{
            .kind = EditorAssetBrowserSelectionKind::Asset,
            .asset = asset.metadata.id,
        });
    }
    return items;
}

} // namespace

bool EditorAssetBrowserState::SelectFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    if (!selection_.SelectFolder(virtualPath, manager)) {
        return false;
    }
    view_.CloseSortMenu();
    view_.CloseFilterMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    CloseDropActionMenu();
    hasContentSelectionAnchor_ = false;
    return true;
}

bool EditorAssetBrowserState::SelectContentFolder(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    if (!selection_.SelectContentFolder(virtualPath, manager)) {
        return false;
    }
    view_.CloseSortMenu();
    view_.CloseFilterMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    CloseDropActionMenu();
    hasContentSelectionAnchor_ = false;
    return true;
}

bool EditorAssetBrowserState::SelectContentFolderAt(std::size_t index, const kb::assets::AssetManager& manager, bool additive, bool range) {
    const std::vector<ContentSelectionItem> items = VisibleContentItems(*this, manager);
    if (index >= items.size() || items[index].kind != EditorAssetBrowserSelectionKind::Folder) {
        return false;
    }

    if (range && hasContentSelectionAnchor_ && contentSelectionAnchor_ < items.size()) {
        if (!additive) {
            selection_.ClearContentSelection();
        }
        const std::size_t first = std::min(contentSelectionAnchor_, index);
        const std::size_t last = std::max(contentSelectionAnchor_, index);
        for (std::size_t item = first; item <= last; ++item) {
            if (items[item].kind == EditorAssetBrowserSelectionKind::Folder) {
                static_cast<void>(selection_.AddContentFolder(items[item].folder, manager));
            } else if (items[item].kind == EditorAssetBrowserSelectionKind::Asset) {
                static_cast<void>(selection_.AddAsset(items[item].asset, manager));
            }
        }
        static_cast<void>(selection_.AddContentFolder(items[index].folder, manager));
    } else if (additive) {
        static_cast<void>(selection_.ToggleContentFolder(items[index].folder, manager));
    } else {
        static_cast<void>(selection_.SelectContentFolder(items[index].folder, manager));
    }

    contentSelectionAnchor_ = index;
    hasContentSelectionAnchor_ = true;
    view_.CloseSortMenu();
    view_.CloseFilterMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    CloseDropActionMenu();
    return true;
}

bool EditorAssetBrowserState::SelectAsset(kb::assets::AssetId id, const kb::assets::AssetManager& manager) {
    if (!selection_.SelectAsset(id, manager)) {
        return false;
    }
    inspectorAsset_ = id;
    view_.CloseSortMenu();
    view_.CloseFilterMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    CloseDropActionMenu();
    hasContentSelectionAnchor_ = false;
    return true;
}

bool EditorAssetBrowserState::SelectAssetAt(std::size_t index, const kb::assets::AssetManager& manager, bool additive, bool range) {
    const std::vector<ContentSelectionItem> items = VisibleContentItems(*this, manager);
    const std::size_t globalIndex = ChildFolderRows(manager).size() + index;
    if (globalIndex >= items.size() || items[globalIndex].kind != EditorAssetBrowserSelectionKind::Asset) {
        return false;
    }

    if (range && hasContentSelectionAnchor_ && contentSelectionAnchor_ < items.size()) {
        if (!additive) {
            selection_.ClearContentSelection();
        }
        const std::size_t first = std::min(contentSelectionAnchor_, globalIndex);
        const std::size_t last = std::max(contentSelectionAnchor_, globalIndex);
        for (std::size_t item = first; item <= last; ++item) {
            if (items[item].kind == EditorAssetBrowserSelectionKind::Folder) {
                static_cast<void>(selection_.AddContentFolder(items[item].folder, manager));
            } else if (items[item].kind == EditorAssetBrowserSelectionKind::Asset) {
                static_cast<void>(selection_.AddAsset(items[item].asset, manager));
            }
        }
        static_cast<void>(selection_.AddAsset(items[globalIndex].asset, manager));
    } else if (additive) {
        static_cast<void>(selection_.ToggleAsset(items[globalIndex].asset, manager));
    } else {
        static_cast<void>(selection_.SelectAsset(items[globalIndex].asset, manager));
    }
    if (selection_.SelectedAsset().IsValid()) {
        inspectorAsset_ = selection_.SelectedAsset();
    }

    contentSelectionAnchor_ = globalIndex;
    hasContentSelectionAnchor_ = true;
    view_.CloseSortMenu();
    view_.CloseFilterMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    CloseDropActionMenu();
    return true;
}

bool EditorAssetBrowserState::SelectAllContent(const kb::assets::AssetManager& manager) {
    const std::vector<ContentSelectionItem> items = VisibleContentItems(*this, manager);
    if (items.empty()) {
        return false;
    }

    selection_.ClearContentSelection();
    for (const ContentSelectionItem& item : items) {
        if (item.kind == EditorAssetBrowserSelectionKind::Folder) {
            static_cast<void>(selection_.AddContentFolder(item.folder, manager));
        } else if (item.kind == EditorAssetBrowserSelectionKind::Asset) {
            static_cast<void>(selection_.AddAsset(item.asset, manager));
        }
    }
    if (selection_.SelectedAsset().IsValid()) {
        inspectorAsset_ = selection_.SelectedAsset();
    }
    contentSelectionAnchor_ = 0;
    hasContentSelectionAnchor_ = true;
    view_.CloseSortMenu();
    view_.CloseFilterMenu();
    contextMenu_.Close();
    deleteConfirm_.Close();
    CloseDropActionMenu();
    return true;
}

bool EditorAssetBrowserState::ToggleFolderExpanded(const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    if (!selection_.ToggleFolderExpanded(virtualPath, manager)) {
        return false;
    }
    view_.CloseSortMenu();
    view_.CloseFilterMenu();
    contextMenu_.Close();
    CloseDropActionMenu();
    return true;
}

void EditorAssetBrowserState::ClearSelection() noexcept {
    selection_.ClearSelection();
    inspectorAsset_ = {};
    hasContentSelectionAnchor_ = false;
    deleteConfirm_.Close();
    CloseDropActionMenu();
}

} // namespace kb::editor
