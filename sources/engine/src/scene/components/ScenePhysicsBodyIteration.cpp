#include "scene/components/SceneComponentIteration.hpp"

#include "ecs/world/WorldInternalAccess.hpp"
#include "scene/components/SceneComponentIterationAccess.hpp"

#include <flecs.h>

namespace kb::scene {
namespace {

[[nodiscard]] ecs_query_t* CreatePhysicsBodyQuery(
    ecs_world_t* world,
    std::uint64_t transformComponentId,
    std::uint64_t rigidbodyComponentId,
    std::uint64_t colliderComponentId) {
    ecs_query_desc_t desc{};
    desc.terms[0].id = transformComponentId;
    desc.terms[1].id = rigidbodyComponentId;
    desc.terms[2].id = colliderComponentId;
    desc.cache_kind = EcsQueryCacheAuto;
    return ecs_query_init(world, &desc);
}

void VisitPhysicsBodyQuery(const kb::ecs::World& world, ecs_query_t* query, PhysicsBodyVisitor visitor, void* context) {
    ecs_iter_t it = ecs_query_iter(world.NativeHandle(), query);
    while (ecs_query_next(&it)) {
        const auto* transforms = SceneComponentIterationAccess::Field<TransformComponent>(it, 0);
        const auto* rigidbodies = SceneComponentIterationAccess::Field<RigidbodyComponent>(it, 1);
        const auto* colliders = SceneComponentIterationAccess::Field<ColliderComponent>(it, 2);
        if (transforms == nullptr || rigidbodies == nullptr || colliders == nullptr) {
            continue;
        }

        for (int32_t index = 0; index < it.count; ++index) {
            const SceneEntity entity = kb::ecs::WorldInternalAccess::ResolveAliveEntity(world, it.entities[index]);
            if (entity.IsValid()) {
                visitor(entity, transforms[index], rigidbodies[index], colliders[index], context);
            }
        }
    }
}

} // namespace

void SceneComponentIteration::ForEachPhysicsBody(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t rigidbodyComponentId,
    std::uint64_t colliderComponentId,
    ecs_query_t*& cachedQuery,
    PhysicsBodyVisitor visitor,
    void* context) {
    if (visitor == nullptr) {
        return;
    }

    if (cachedQuery == nullptr) {
        cachedQuery = CreatePhysicsBodyQuery(
            const_cast<ecs_world_t*>(world.NativeHandle()),
            transformComponentId,
            rigidbodyComponentId,
            colliderComponentId);
    }
    if (cachedQuery == nullptr) {
        return;
    }
    VisitPhysicsBodyQuery(world, cachedQuery, visitor, context);
}

} // namespace kb::scene
