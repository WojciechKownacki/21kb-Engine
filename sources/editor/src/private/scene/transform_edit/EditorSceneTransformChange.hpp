#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::editor {

struct EditorSceneObjectTransformChange {
    kb::scene::SceneEntity entity{};
    kb::scene::TransformComponent before{};
    kb::scene::TransformComponent after{};
    // The local translations of before and after in double precision (docs/large_worlds.md); the float views are
    // before.localPosition and after.localPosition.
    kb::math::DVec3 beforeTranslation{};
    kb::math::DVec3 afterTranslation{};
};

} // namespace kb::editor
