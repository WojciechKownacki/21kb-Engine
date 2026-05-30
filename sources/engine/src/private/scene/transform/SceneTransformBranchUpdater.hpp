#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class SceneComponentRegistry;

class SceneTransformBranchUpdater {
public:
    void Update(kb::ecs::World& world, const SceneComponentRegistry& components, SceneEntity entity, const TransformComponent& parentTransform, bool parentDirty) const;
};

} // namespace kb::scene
