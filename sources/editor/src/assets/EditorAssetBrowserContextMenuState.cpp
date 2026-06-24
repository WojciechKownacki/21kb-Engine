#include "assets/EditorAssetBrowserContextMenuState.hpp"

namespace kb::editor {

bool EditorAssetBrowserContextMenuState::IsOpen() const noexcept {
    return open_;
}

int EditorAssetBrowserContextMenuState::X() const noexcept {
    return x_;
}

int EditorAssetBrowserContextMenuState::Y() const noexcept {
    return y_;
}

EditorAssetContextTargetKind EditorAssetBrowserContextMenuState::TargetKind() const noexcept {
    return targetKind_;
}

kb::assets::AssetId EditorAssetBrowserContextMenuState::TargetAsset() const noexcept {
    return targetAsset_;
}

const std::filesystem::path& EditorAssetBrowserContextMenuState::TargetFolder() const noexcept {
    return targetFolder_;
}

EditorAssetContextCommand EditorAssetBrowserContextMenuState::HoveredCommand() const noexcept {
    return hoveredCommand_;
}

std::vector<EditorAssetContextMenuItem> EditorAssetBrowserContextMenuState::Items(bool targetAssetExists, bool targetFolderCanMutate) const {
    std::vector<EditorAssetContextMenuItem> items;
    if (!open_) {
        return items;
    }

    switch (targetKind_) {
    case EditorAssetContextTargetKind::Asset:
        if (targetAssetExists) {
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Rename, .label = "Rename" });
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Delete, .label = "Delete", .separatorAfter = true });
        }
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Refresh, .label = "Refresh" });
        break;
    case EditorAssetContextTargetKind::Folder:
        if (targetFolderCanMutate) {
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Rename, .label = "Rename" });
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Delete, .label = "Delete", .separatorAfter = true });
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Import, .label = "Import...", .separatorAfter = true });
        } else {
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Import, .label = "Import..." });
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewFolder, .label = "New Folder", .separatorAfter = true });
        }
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewLuaScript, .label = "New Lua Script" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewMaterial, .label = "New Material" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputAction, .label = "New Input Action" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputAxis, .label = "New Input Axis" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputMappingContext, .label = "New Input Mapping Context", .separatorAfter = true });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::AddLighting, .label = "Add", .separatorAfter = true });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Refresh, .label = "Refresh" });
        break;
    case EditorAssetContextTargetKind::Background:
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Import, .label = "Import..." });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewFolder, .label = "New Folder", .separatorAfter = true });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewLuaScript, .label = "New Lua Script" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewMaterial, .label = "New Material" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputAction, .label = "New Input Action" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputAxis, .label = "New Input Axis" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputMappingContext, .label = "New Input Mapping Context", .separatorAfter = true });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::AddLighting, .label = "Add", .separatorAfter = true });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Refresh, .label = "Refresh" });
        break;
    case EditorAssetContextTargetKind::None:
    default:
        break;
    }
    return items;
}

void EditorAssetBrowserContextMenuState::OpenForBackground(int x, int y, const std::filesystem::path& selectedFolder) {
    open_ = true;
    x_ = x;
    y_ = y;
    targetKind_ = EditorAssetContextTargetKind::Background;
    targetAsset_ = {};
    targetFolder_ = selectedFolder;
    hoveredCommand_ = EditorAssetContextCommand::None;
}

void EditorAssetBrowserContextMenuState::OpenForAsset(int x, int y, kb::assets::AssetId id) {
    open_ = true;
    x_ = x;
    y_ = y;
    targetKind_ = EditorAssetContextTargetKind::Asset;
    targetAsset_ = id;
    targetFolder_.clear();
    hoveredCommand_ = EditorAssetContextCommand::None;
}

void EditorAssetBrowserContextMenuState::OpenForFolder(int x, int y, const std::filesystem::path& virtualFolder) {
    open_ = true;
    x_ = x;
    y_ = y;
    targetKind_ = EditorAssetContextTargetKind::Folder;
    targetAsset_ = {};
    targetFolder_ = virtualFolder;
    hoveredCommand_ = EditorAssetContextCommand::None;
}

void EditorAssetBrowserContextMenuState::Close() noexcept {
    open_ = false;
    x_ = 0;
    y_ = 0;
    targetKind_ = EditorAssetContextTargetKind::None;
    targetAsset_ = {};
    targetFolder_.clear();
    hoveredCommand_ = EditorAssetContextCommand::None;
}

bool EditorAssetBrowserContextMenuState::SetHoveredCommand(EditorAssetContextCommand command) noexcept {
    if (hoveredCommand_ == command) {
        return false;
    }
    hoveredCommand_ = command;
    return true;
}

} // namespace kb::editor
