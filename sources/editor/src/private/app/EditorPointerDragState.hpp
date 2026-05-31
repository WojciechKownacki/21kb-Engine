#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <filesystem>

namespace kb::editor {

enum class EditorPointerDragKind {
    None,
    HierarchyEntity,
    PrefabAsset,
};

struct EditorPointerDragState {
    EditorPointerDragKind kind = EditorPointerDragKind::None;
    kb::scene::SceneEntity entity{};
    std::filesystem::path assetPath{};
    int x = 0;
    int y = 0;

    [[nodiscard]] bool Active() const noexcept {
        return kind != EditorPointerDragKind::None;
    }

    void Clear() {
        kind = EditorPointerDragKind::None;
        entity = {};
        assetPath.clear();
        x = 0;
        y = 0;
    }
};

} // namespace kb::editor
