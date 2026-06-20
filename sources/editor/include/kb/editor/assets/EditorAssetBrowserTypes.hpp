#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"

#include <filesystem>
#include <string>

namespace kb::editor {

enum class EditorAssetViewMode {
    List,
    Tiles,
};

enum class EditorAssetSortMode {
    Name,
    Type,
    Path,
};

enum class EditorAssetTextEditMode {
    None,
    NewFolder,
    RenameAsset,
    RenameFolder,
};

enum class EditorAssetBrowserSelectionKind {
    None,
    Folder,
    Asset,
};

enum class EditorAssetContextTargetKind {
    None,
    Background,
    Folder,
    Asset,
};

enum class EditorAssetContextCommand {
    None,
    Import,
    NewFolder,
    NewLuaScript,
    NewInputAction,
    NewInputAxis,
    NewInputMappingContext,
    AddLighting,
    AddDirectionalLight,
    AddPointLight,
    AddSpotLight,
    Rename,
    Delete,
    Refresh,
};

enum class EditorAssetDropAction {
    None,
    MoveHere,
    CopyHere,
};

struct EditorAssetContextMenuItem {
    EditorAssetContextCommand command = EditorAssetContextCommand::None;
    const char* label = "";
    bool separatorAfter = false;
};

struct EditorAssetFolderRow {
    std::filesystem::path virtualPath;
    std::string name;
    int depth = 0;
    bool selected = false;
    bool hasChildren = false;
    bool expanded = false;
};

struct EditorAssetItemRow {
    kb::assets::AssetMetadata metadata;
    bool selected = false;
    bool loaded = false;
};

struct EditorAssetSelectionSummaryRow {
    std::string key;
    std::string id;
    std::string name;
    std::string objectType;
    bool checked = true;
};

} // namespace kb::editor
