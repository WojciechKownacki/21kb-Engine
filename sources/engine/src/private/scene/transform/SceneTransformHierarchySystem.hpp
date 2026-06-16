#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

class SceneComponentRegistry;

class SceneTransformHierarchySystem {
public:
    void Update(SceneState& state) const;
};

} // namespace kb::scene
