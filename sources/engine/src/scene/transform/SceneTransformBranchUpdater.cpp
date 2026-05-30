#include "scene/transform/SceneTransformBranchUpdater.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"
#include "scene/transform/TransformMath.hpp"

#include <flecs.h>

namespace kb::scene {

void SceneTransformBranchUpdater::Update(
    kb::ecs::World& world,
    const SceneComponentRegistry& components,
    SceneEntity entity,
    const TransformComponent& parentTransform,
    bool parentDirty) const {
    TransformComponent* transform = SceneComponentStorageAccess::TryGetMutable<TransformComponent>(world.NativeHandle(), entity, components.TransformComponentId());
    if (transform == nullptr) {
        return;
    }

    const bool shouldUpdate = parentDirty || transform->worldDirty;
    if (shouldUpdate) {
        *transform = TransformMath::Compose(parentTransform, *transform);
        SceneComponentAccess::MarkModified(world.NativeHandle(), entity, components.TransformComponentId());
    }

    ecs_iter_t it = ecs_children(world.NativeHandle(), entity.Id());
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; ++i) {
            Update(world, components, SceneEntity{ it.entities[i] }, *transform, shouldUpdate);
        }
    }
}

} // namespace kb::scene
