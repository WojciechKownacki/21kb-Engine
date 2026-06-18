#include "scene/components/SceneMeshRendererIterationDispatcher.hpp"

#include "engine/scene/VisibilityComponent.hpp"
#include "ecs/world/WorldInternalAccess.hpp"
#include "scene/components/SceneComponentIterationAccess.hpp"

#include <flecs.h>

namespace kb::scene {
namespace {

[[nodiscard]] ecs_query_t* CreateRendererQuery(
    ecs_world_t* world,
    std::uint64_t transformComponentId,
    std::uint64_t visibilityComponentId,
    std::uint64_t meshRendererComponentId,
    bool visibleOnly) {
    ecs_query_desc_t desc{};
    desc.terms[0].id = meshRendererComponentId;
    desc.terms[1].id = transformComponentId;
    if (visibleOnly) {
        desc.terms[2].id = visibilityComponentId;
        desc.terms[2].oper = EcsOptional;
    }
    desc.cache_kind = EcsQueryCacheAuto;
    return ecs_query_init(world, &desc);
}

void VisitRendererQuery(
    const kb::ecs::World& world,
    ecs_query_t* query,
    bool visibleOnly,
    MeshRendererVisitor visitor,
    void* context) {
    ecs_iter_t it = ecs_query_iter(world.NativeHandle(), query);
    while (ecs_query_next(&it)) {
        const auto* renderers = SceneComponentIterationAccess::Field<MeshRendererComponent>(it, 0);
        const auto* transforms = SceneComponentIterationAccess::Field<TransformComponent>(it, 1);
        const bool hasVisibility = visibleOnly && ecs_field_is_set(&it, 2);
        const auto* visibility = hasVisibility ? SceneComponentIterationAccess::Field<VisibilityComponent>(it, 2) : nullptr;
        if (renderers == nullptr || transforms == nullptr) {
            continue;
        }

        for (int32_t i = 0; i < it.count; ++i) {
            if (visibility != nullptr && !visibility[i].visible) {
                continue;
            }

            const SceneEntity entity = kb::ecs::WorldInternalAccess::ResolveAliveEntity(world, it.entities[i]);
            if (!entity.IsValid()) {
                continue;
            }
            visitor(entity, transforms[i], renderers[i], context);
        }
    }
}

} // namespace

void SceneMeshRendererIterationDispatcher::ForEach(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t visibilityComponentId,
    std::uint64_t meshRendererComponentId,
    bool visibleOnly,
    ecs_query_t*& cachedQuery,
    MeshRendererVisitor visitor,
    void* context) {
    if (visitor == nullptr) {
        return;
    }

    if (cachedQuery == nullptr) {
        cachedQuery = CreateRendererQuery(
            const_cast<ecs_world_t*>(world.NativeHandle()),
            transformComponentId,
            visibilityComponentId,
            meshRendererComponentId,
            visibleOnly);
    }
    if (cachedQuery == nullptr) {
        return;
    }
    VisitRendererQuery(world, cachedQuery, visibleOnly, visitor, context);
}

} // namespace kb::scene
