#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <string>
#include <vector>

namespace kb::editor {

struct EditorSceneObjectTransformChange {
    kb::scene::SceneEntity entity{};
    kb::scene::TransformComponent before{};
    kb::scene::TransformComponent after{};
};

struct EditorSceneObjectPrefabPayload {
    kb::scene::ScenePrefab prefab{};
    kb::scene::SceneEntity parent{};
};

struct EditorSceneActiveTransformEdit {
    std::string label;
    kb::scene::SceneEntity primary{};
    std::vector<EditorSceneObjectTransformChange> changes;

    [[nodiscard]] bool Active() const noexcept {
        return primary.IsValid() && !changes.empty();
    }

    void Clear() noexcept {
        label.clear();
        primary = {};
        changes.clear();
    }
};

} // namespace kb::editor
