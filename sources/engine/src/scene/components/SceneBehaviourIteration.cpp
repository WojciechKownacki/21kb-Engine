#include "scene/components/SceneComponentIteration.hpp"

#include "ecs/world/WorldInternalAccess.hpp"
#include "scene/components/SceneComponentIterationAccess.hpp"

#include <flecs.h>

namespace kb::scene {

void SceneComponentIteration::ForEachBehaviour(const kb::ecs::World& world, std::uint64_t behaviourComponentId, BehaviourVisitor visitor, void* context) {
    if (visitor == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_each_id(world.NativeHandle(), behaviourComponentId);
    while (ecs_each_next(&it)) {
        const auto* behaviours = SceneComponentIterationAccess::Field<BehaviourComponent>(it, 0);
        if (behaviours == nullptr) {
            continue;
        }

        for (int32_t i = 0; i < it.count; ++i) {
            const SceneEntity entity = kb::ecs::WorldInternalAccess::ResolveAliveEntity(world, it.entities[i]);
            if (entity.IsValid()) {
                visitor(entity, behaviours[i], context);
            }
        }
    }
}

} // namespace kb::scene
