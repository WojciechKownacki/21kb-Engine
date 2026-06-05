#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::editor {

enum class EditorPointerDragKind {
    None,
    HierarchyEntity,
    PrefabAsset,
    AssetFolder,
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
    bool assetAddsBehaviour = false;
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
        assetAddsBehaviour = false;
        dragging = false;
        startX = 0;
        startY = 0;
        x = 0;
        y = 0;
    }
};

} // namespace kb::editor
