#include "assets/EditorAssetBrowserContextMenuState.hpp"
#include "localization/EditorLocalization.hpp"

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
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Rename, .label = std::string{ EditorText("common.rename", "Rename") } });
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Delete, .label = std::string{ EditorText("common.delete", "Delete") }, .separatorAfter = true });
        }
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Refresh, .label = std::string{ EditorText("projectFiles.menu.refresh", "Refresh") } });
        break;
    case EditorAssetContextTargetKind::Folder:
        if (targetFolderCanMutate) {
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Rename, .label = std::string{ EditorText("common.rename", "Rename") } });
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Delete, .label = std::string{ EditorText("common.delete", "Delete") }, .separatorAfter = true });
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Import, .label = std::string{ EditorText("projectFiles.menu.import", "Import...") }, .separatorAfter = true });
        } else {
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Import, .label = std::string{ EditorText("projectFiles.menu.import", "Import...") } });
            items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewFolder, .label = std::string{ EditorText("projectFiles.menu.newFolder", "New Folder") }, .separatorAfter = true });
        }
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewNativeScript, .label = std::string{ EditorText("projectFiles.menu.newNativeScript", "New Script (C++)") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewLuaScript, .label = std::string{ EditorText("projectFiles.menu.newLuaScript", "New Lua Script") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewMaterial, .label = std::string{ EditorText("projectFiles.menu.newMaterial", "New Material") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewMaterialFunction, .label = std::string{ EditorText("projectFiles.menu.newMaterialFunction", "New Material Function") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewMaterialType, .label = std::string{ EditorText("projectFiles.menu.newMaterialType", "New Material Type") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputAction, .label = std::string{ EditorText("projectFiles.menu.newInputAction", "New Input Action") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputAxis, .label = std::string{ EditorText("projectFiles.menu.newInputAxis", "New Input Axis") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputMappingContext, .label = std::string{ EditorText("projectFiles.menu.newInputMappingContext", "New Input Mapping Context") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewAudioMixer, .label = std::string{ EditorText("projectFiles.menu.newAudioMixer", "New Audio Mixer") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewPostProcessProfile, .label = std::string{ EditorText("projectFiles.menu.newPostProcessProfile", "New Post-Process Profile") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewImportPreset, .label = std::string{ EditorText("projectFiles.menu.newImportPreset", "New Import Preset") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewBlendSpace2D, .label = "New BlendSpace2D" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewGameplayGraph, .label = "New Gameplay Graph" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewParticleEffect, .label = std::string{ EditorText("projectFiles.menu.newParticleEffect", "New Particle Effect") }, .separatorAfter = true });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::AddLighting, .label = std::string{ EditorText("projectFiles.menu.add", "Add") }, .separatorAfter = true });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Refresh, .label = std::string{ EditorText("projectFiles.menu.refresh", "Refresh") } });
        break;
    case EditorAssetContextTargetKind::Background:
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Import, .label = std::string{ EditorText("projectFiles.menu.import", "Import...") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewFolder, .label = std::string{ EditorText("projectFiles.menu.newFolder", "New Folder") }, .separatorAfter = true });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewNativeScript, .label = std::string{ EditorText("projectFiles.menu.newNativeScript", "New Script (C++)") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewLuaScript, .label = std::string{ EditorText("projectFiles.menu.newLuaScript", "New Lua Script") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewMaterial, .label = std::string{ EditorText("projectFiles.menu.newMaterial", "New Material") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewMaterialFunction, .label = std::string{ EditorText("projectFiles.menu.newMaterialFunction", "New Material Function") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewMaterialType, .label = std::string{ EditorText("projectFiles.menu.newMaterialType", "New Material Type") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputAction, .label = std::string{ EditorText("projectFiles.menu.newInputAction", "New Input Action") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputAxis, .label = std::string{ EditorText("projectFiles.menu.newInputAxis", "New Input Axis") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewInputMappingContext, .label = std::string{ EditorText("projectFiles.menu.newInputMappingContext", "New Input Mapping Context") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewAudioMixer, .label = std::string{ EditorText("projectFiles.menu.newAudioMixer", "New Audio Mixer") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewPostProcessProfile, .label = std::string{ EditorText("projectFiles.menu.newPostProcessProfile", "New Post-Process Profile") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewImportPreset, .label = std::string{ EditorText("projectFiles.menu.newImportPreset", "New Import Preset") } });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewBlendSpace2D, .label = "New BlendSpace2D" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewGameplayGraph, .label = "New Gameplay Graph" });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::NewParticleEffect, .label = std::string{ EditorText("projectFiles.menu.newParticleEffect", "New Particle Effect") }, .separatorAfter = true });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::AddLighting, .label = std::string{ EditorText("projectFiles.menu.add", "Add") }, .separatorAfter = true });
        items.push_back(EditorAssetContextMenuItem{ .command = EditorAssetContextCommand::Refresh, .label = std::string{ EditorText("projectFiles.menu.refresh", "Refresh") } });
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
