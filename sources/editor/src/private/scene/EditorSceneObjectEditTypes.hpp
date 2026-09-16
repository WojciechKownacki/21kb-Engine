#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "scene/transform_edit/EditorSceneTransformChange.hpp"

#include "engine/scene/SceneEntity.hpp"

#include <vector>

namespace kb::editor {

struct EditorSceneObjectPrefabPayload {
    kb::scene::ScenePrefab prefab{};
    kb::scene::SceneEntity parent{};
    // The live entity behind each prefab node, in node order, when the capture took the whole subtree.
    // Restoring hands every node a new entity, and references held by objects outside the payload -
    // UI navigation links - are moved from these ids onto the restored ones. Empty when the prefab
    // skipped part of the subtree, because node order then no longer matches the hierarchy walk.
    std::vector<kb::scene::SceneEntity> capturedEntities;
};

} // namespace kb::editor
