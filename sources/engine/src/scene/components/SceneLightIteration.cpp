#include "scene/components/SceneComponentIteration.hpp"

#include "ecs/world/WorldInternalAccess.hpp"
#include "scene/components/SceneComponentIterationAccess.hpp"

#include <flecs.h>

namespace kb::scene {
namespace {

[[nodiscard]] ecs_query_t* CreateLightQuery(ecs_world_t* world, std::uint64_t transformComponentId, std::uint64_t lightComponentId) {
    ecs_query_desc_t desc{};
    desc.terms[0].id = lightComponentId;
    desc.terms[1].id = transformComponentId;
    desc.cache_kind = EcsQueryCacheAuto;
    return ecs_query_init(world, &desc);
}

void VisitLightQuery(const kb::ecs::World& world, ecs_query_t* query, LightVisitor visitor, void* context) {
    ecs_iter_t it = ecs_query_iter(world.NativeHandle(), query);
    while (ecs_query_next(&it)) {
        const auto* lights = SceneComponentIterationAccess::Field<LightComponent>(it, 0);
        const auto* transforms = SceneComponentIterationAccess::Field<TransformComponent>(it, 1);
        if (lights == nullptr || transforms == nullptr) {
            continue;
        }

        for (int32_t i = 0; i < it.count; ++i) {
            const SceneEntity entity = kb::ecs::WorldInternalAccess::ResolveAliveEntity(world, it.entities[i]);
            if (entity.IsValid()) {
                visitor(entity, transforms[i], lights[i], context);
            }
        }
    }
}

} // namespace

void SceneComponentIteration::ForEachLight(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t lightComponentId,
    ecs_query_t*& cachedQuery,
    LightVisitor visitor,
    void* context) {
    if (visitor == nullptr) {
        return;
    }

    if (cachedQuery == nullptr) {
        cachedQuery = CreateLightQuery(const_cast<ecs_world_t*>(world.NativeHandle()), transformComponentId, lightComponentId);
    }
    if (cachedQuery == nullptr) {
        return;
    }
    VisitLightQuery(world, cachedQuery, visitor, context);
}

} // namespace kb::scene
