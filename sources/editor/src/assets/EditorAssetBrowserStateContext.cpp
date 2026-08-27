#include "assets/EditorAssetBrowserState.hpp"

#include "assets/EditorAssetBrowserPathUtils.hpp"

#include "assets/EditorAssetOpenPolicy.hpp"

#include "engine/scene/ParticleEffectAssetIO.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

[[nodiscard]] bool IsMaterialAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterial"
        || metadata.type == "RenderMaterialInstance"
        || metadata.type == "RenderMaterialFunction"
        || metadata.type == "RenderMaterialGraph"
        || metadata.type == "RenderMaterialType";
}

// Commands that only read. Everything else writes to disk, and outside the project's
// own content there is nothing the editor may rewrite.
[[nodiscard]] bool IsReadOnlyCommand(EditorAssetContextCommand command) noexcept {
    return command == EditorAssetContextCommand::Open || command == EditorAssetContextCommand::FindReferences ||
        command == EditorAssetContextCommand::Refresh;
}

[[nodiscard]] std::vector<EditorAssetContextMenuItem> MaterialContextMenuItems(const kb::assets::AssetMetadata& metadata) {
    std::vector<EditorAssetContextMenuItem> items;
    if (metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance" || metadata.type == "RenderMaterialGraph") {
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Open, .label = "Open" });
    }
    if (metadata.type == "RenderMaterial") {
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Duplicate, .label = "Duplicate" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::CreateMaterialInstance, .label = "Create Material Instance", .separatorAfter = true });
    } else if (metadata.type == "RenderMaterialGraph") {
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::CreateMaterialFromGraph, .label = "Create Material From Graph", .separatorAfter = true });
    } else if (metadata.type == "RenderMaterialType") {
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::CreateMaterialFromMaterialType, .label = "Create Material From Material Type", .separatorAfter = true });
    }
    items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Rename, .label = "Rename" });
    items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Delete, .label = "Delete" });
    items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::FindReferences, .label = "Find References", .separatorAfter = true });
    items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Refresh, .label = "Refresh" });
    return items;
}

} // namespace

std::vector<EditorAssetContextMenuItem> EditorAssetBrowserState::ContextMenuItems(const kb::assets::AssetManager& manager) const {
    const bool assetExists = manager.Registry().Find(contextMenu_.TargetAsset()) != nullptr;
    std::vector<EditorAssetContextMenuItem> items = contextMenu_.Items(assetExists, ContextMenuTargetFolderCanMutate(manager));
    if (contextMenu_.TargetKind() == EditorAssetContextTargetKind::Asset) {
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(contextMenu_.TargetAsset());
        if (metadata != nullptr && IsMaterialAsset(*metadata)) {
            items = MaterialContextMenuItems(*metadata);
        } else if (metadata != nullptr) {
            items.insert(items.begin(), EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Duplicate, .label = "Duplicate", .separatorAfter = true });
            if (EditorAssetOpenPolicy::CanOpen(*metadata)) {
                items.insert(items.begin(), EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Open, .label = "Open" });
            }
        }
        if (metadata != nullptr && (metadata->type == "RenderMesh" || metadata->importCategory == "Mesh")) {
            items.insert(items.begin(), EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::ExtractMaterials, .label = "Extract Material Instances", .separatorAfter = true });
        }
        // Particle effects are the second asset kind whose scene usage can be
        // queried, so they carry the same entry the material menu already has,
        // in the same place: after Delete and before Refresh.
        if (metadata != nullptr && metadata->type == kb::scene::kParticleEffectAssetType) {
            const auto refresh = std::ranges::find(items, EditorAssetContextCommand::Refresh, &EditorAssetContextMenuItem::command);
            items.insert(refresh, EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::FindReferences, .label = "Find References", .separatorAfter = true });
        }
    }
    if (!ContextMenuTargetIsProjectContent(manager)) {
        std::erase_if(items, [](const EditorAssetContextMenuItem& item) { return !IsReadOnlyCommand(item.command); });
    }
    return items;
}

bool EditorAssetBrowserState::ContextMenuTargetIsProjectContent(const kb::assets::AssetManager& manager) const {
    if (contextMenu_.TargetKind() == EditorAssetContextTargetKind::Asset) {
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(contextMenu_.TargetAsset());
        return metadata != nullptr && asset_browser::IsProjectContent(metadata->virtualPath);
    }
    return asset_browser::IsProjectContent(contextMenu_.TargetFolder());
}

void EditorAssetBrowserState::OpenContextMenuForBackground(int x, int y) {
    contextMenu_.OpenForBackground(x, y, selection_.SelectedFolder());
    deleteConfirm_.Close();
    CloseDropActionMenu();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
}

bool EditorAssetBrowserState::OpenContextMenuForAsset(int x, int y, kb::assets::AssetId id, const kb::assets::AssetManager& manager) {
    if (manager.Registry().Find(id) == nullptr) {
        return false;
    }

    contextMenu_.OpenForAsset(x, y, id);
    deleteConfirm_.Close();
    CloseDropActionMenu();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
    return true;
}

bool EditorAssetBrowserState::OpenContextMenuForFolder(int x, int y, const std::filesystem::path& virtualPath, const kb::assets::AssetManager& manager) {
    const std::filesystem::path normalized{ asset_browser::Normalize(virtualPath) };
    if (!selection_.FolderExists(normalized, manager)) {
        return false;
    }

    contextMenu_.OpenForFolder(x, y, normalized);
    deleteConfirm_.Close();
    CloseDropActionMenu();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
    return true;
}

void EditorAssetBrowserState::CloseContextMenu() noexcept {
    contextMenu_.Close();
}

void EditorAssetBrowserState::OpenDropActionMenuForAsset(int x, int y, kb::assets::AssetId id, const std::filesystem::path& targetFolder) {
    dropActionMenuOpen_ = true;
    dropActionMenuX_ = x;
    dropActionMenuY_ = y;
    dropActionAsset_ = id;
    dropActionSourceFolder_.clear();
    dropActionTargetFolder_ = targetFolder;
    dropActionHovered_ = EditorAssetDropAction::None;
    contextMenu_.Close();
    deleteConfirm_.Close();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
}

void EditorAssetBrowserState::OpenDropActionMenuForFolder(int x, int y, const std::filesystem::path& sourceFolder, const std::filesystem::path& targetFolder) {
    dropActionMenuOpen_ = true;
    dropActionMenuX_ = x;
    dropActionMenuY_ = y;
    dropActionAsset_ = {};
    dropActionSourceFolder_ = sourceFolder;
    dropActionTargetFolder_ = targetFolder;
    dropActionHovered_ = EditorAssetDropAction::None;
    contextMenu_.Close();
    deleteConfirm_.Close();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
}

void EditorAssetBrowserState::CloseDropActionMenu() noexcept {
    dropActionMenuOpen_ = false;
    dropActionHovered_ = EditorAssetDropAction::None;
}

bool EditorAssetBrowserState::OpenDeleteConfirm() noexcept {
    if (selection_.SelectionKind() == EditorAssetBrowserSelectionKind::None || !selection_.IsSelectionFocused()) {
        return false;
    }
    deleteConfirmListScrollOffset_ = 0;
    deleteConfirmListScrollbarDragging_ = false;
    uncheckedDeleteTargets_.clear();
    deleteConfirm_.Open();
    view_.FocusSearch(false);
    view_.CloseFilterMenu();
    view_.CloseSortMenu();
    contextMenu_.Close();
    CloseDropActionMenu();
    return true;
}

void EditorAssetBrowserState::CloseDeleteConfirm() noexcept {
    deleteConfirmListScrollOffset_ = 0;
    deleteConfirmListScrollbarDragging_ = false;
    uncheckedDeleteTargets_.clear();
    deleteConfirm_.Close();
}

void EditorAssetBrowserState::BeginDeleteConfirmDrag(int x, int y) noexcept {
    deleteConfirm_.BeginDrag(x, y);
}

void EditorAssetBrowserState::DragDeleteConfirm(int x, int y) noexcept {
    deleteConfirm_.Drag(x, y);
}

void EditorAssetBrowserState::EndDeleteConfirmDrag() noexcept {
    deleteConfirm_.EndDrag();
}

bool EditorAssetBrowserState::SetContextMenuHoveredCommand(EditorAssetContextCommand command) noexcept {
    return contextMenu_.SetHoveredCommand(command);
}

bool EditorAssetBrowserState::SetDropActionHoveredCommand(EditorAssetDropAction command) noexcept {
    if (dropActionHovered_ == command) {
        return false;
    }
    dropActionHovered_ = command;
    return true;
}

bool EditorAssetBrowserState::ContextMenuTargetFolderCanMutate(const kb::assets::AssetManager& manager) const {
    return contextMenu_.TargetKind() == EditorAssetContextTargetKind::Folder
        && asset_browser::Normalize(contextMenu_.TargetFolder()) != "/Game"
        && selection_.FolderExists(contextMenu_.TargetFolder(), manager);
}

} // namespace kb::editor
