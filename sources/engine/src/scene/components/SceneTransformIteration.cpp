#include "scene/components/SceneComponentIteration.hpp"

#include "ecs/world/WorldInternalAccess.hpp"
#include "scene/components/SceneComponentIterationAccess.hpp"

#include <flecs.h>

namespace kb::scene {

void SceneComponentIteration::ForEachTransform(const kb::ecs::World& world, std::uint64_t transformComponentId, ConstTransformVisitor visitor, void* context) {
    if (visitor == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_each_id(world.NativeHandle(), transformComponentId);
    while (ecs_each_next(&it)) {
        const auto* transforms = SceneComponentIterationAccess::Field<TransformComponent>(it, 0);
        if (transforms == nullptr) {
            continue;
        }

        for (int32_t i = 0; i < it.count; ++i) {
            const SceneEntity entity = kb::ecs::WorldInternalAccess::ResolveAliveEntity(world, it.entities[i]);
            if (entity.IsValid()) {
                visitor(entity, transforms[i], context);
            }
        }
    }
}

void SceneComponentIteration::ForEachMutableTransform(kb::ecs::World& world, std::uint64_t transformComponentId, MutableTransformVisitor visitor, void* context) {
    if (visitor == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_each_id(world.NativeHandle(), transformComponentId);
    while (ecs_each_next(&it)) {
        auto* transforms = SceneComponentIterationAccess::MutableField<TransformComponent>(it, 0);
        if (transforms == nullptr) {
            continue;
        }

        for (int32_t i = 0; i < it.count; ++i) {
            const SceneEntity entity = kb::ecs::WorldInternalAccess::ResolveAliveEntity(world, it.entities[i]);
            if (entity.IsValid()) {
                visitor(entity, transforms[i], context);
            }
        }
    }
}

} // namespace kb::scene
