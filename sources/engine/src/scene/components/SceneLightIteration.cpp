#include "scene/components/SceneComponentIteration.hpp"

#include "scene/components/SceneComponentIterationAccess.hpp"

#include <flecs.h>

namespace kb::scene {

void SceneComponentIteration::ForEachLight(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t lightComponentId,
    LightVisitor visitor,
    void* context) {
    if (visitor == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_each_id(world.NativeHandle(), lightComponentId);
    while (ecs_each_next(&it)) {
        const auto* lights = SceneComponentIterationAccess::Field<LightComponent>(it, 0);
        if (lights == nullptr) {
            continue;
        }

        for (int32_t i = 0; i < it.count; ++i) {
            const SceneEntity entity{ it.entities[i] };
            const auto* transform = SceneComponentIterationAccess::TryGet<TransformComponent>(world, entity, transformComponentId);
            if (transform != nullptr) {
                visitor(entity, *transform, lights[i], context);
            }
        }
    }
}

} // namespace kb::scene
