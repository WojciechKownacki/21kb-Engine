#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "scene/material/MaterialEditorState.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::editor {

enum class EditorPointerDragKind {
    None,
    HierarchyEntity,
    PrefabAsset,
    AssetFolder,
    MaterialGraphPaletteCommand,
};

struct EditorPointerDragState {
    EditorPointerDragKind kind = EditorPointerDragKind::None;
    kb::scene::SceneEntity entity{};
    std::vector<kb::scene::SceneEntity> entities{};
    kb::assets::AssetId assetId{};
    std::filesystem::path assetPath{};
    std::filesystem::path assetVirtualPath{};
    std::filesystem::path assetFolderPath{};
    std::string assetLabel{};
    bool assetInstantiatesPrefab = false;
    bool assetCreatesMeshEntity = false;
    bool assetAddsBehaviour = false;
    bool assetAssignsAudioClip = false;
    bool assetAssignsAudioMixer = false;
    bool assetAssignsMaterial = false;
    bool assetAssignsMaterialGraph = false;
    bool assetAssignsTexture = false;
    kb::scene::SceneEntity meshScenePreview{};
    kb::assets::AssetId materialGraphAssetId{};
    MaterialEditorGraphMenuCommand materialGraphCommand = MaterialEditorGraphMenuCommand::None;
    bool meshScenePreviewCommitted = false;
    bool meshPreviewUpdatePending = false;
    void* dragSourceWindow = nullptr;
    bool overlayDirty = false;
    bool dragging = false;
    int startX = 0;
    int startY = 0;
    int x = 0;
    int y = 0;

    [[nodiscard]] bool Active() const noexcept {
        return kind != EditorPointerDragKind::None && dragging;
    }

    [[nodiscard]] bool Potential() const noexcept {
        return kind != EditorPointerDragKind::None;
    }

    void Clear() {
        kind = EditorPointerDragKind::None;
        entity = {};
        entities.clear();
        assetId = {};
        assetPath.clear();
        assetVirtualPath.clear();
        assetFolderPath.clear();
        assetLabel.clear();
        assetInstantiatesPrefab = false;
        assetCreatesMeshEntity = false;
        assetAddsBehaviour = false;
        assetAssignsAudioClip = false;
        assetAssignsAudioMixer = false;
        assetAssignsMaterial = false;
        assetAssignsMaterialGraph = false;
        assetAssignsTexture = false;
        meshScenePreview = {};
        materialGraphAssetId = {};
        materialGraphCommand = MaterialEditorGraphMenuCommand::None;
        meshScenePreviewCommitted = false;
        meshPreviewUpdatePending = false;
        dragSourceWindow = nullptr;
        overlayDirty = false;
        dragging = false;
        startX = 0;
        startY = 0;
        x = 0;
        y = 0;
    }
};

} // namespace kb::editor
