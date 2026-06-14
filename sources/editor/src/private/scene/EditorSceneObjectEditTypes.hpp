#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "scene/transform_edit/EditorSceneTransformChange.hpp"

#include "engine/scene/SceneEntity.hpp"

namespace kb::editor {

struct EditorSceneObjectPrefabPayload {
    kb::scene::ScenePrefab prefab{};
    kb::scene::SceneEntity parent{};
};

} // namespace kb::editor
