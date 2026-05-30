#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class SceneComponentRegistry;

class SceneTransformHierarchySystem {
public:
    void Update(kb::ecs::World& world, const SceneComponentRegistry& components) const;
};

} // namespace kb::scene
