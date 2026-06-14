#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::editor {

struct EditorSceneObjectTransformChange {
    kb::scene::SceneEntity entity{};
    kb::scene::TransformComponent before{};
    kb::scene::TransformComponent after{};
};

} // namespace kb::editor
