#include "scene/components/SceneComponentIteration.hpp"

#include "scene/components/SceneComponentIterationAccess.hpp"

#include <flecs.h>

namespace kb::scene {

void SceneComponentIteration::ForEachMeshRenderer(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t meshRendererComponentId,
    MeshRendererVisitor visitor,
    void* context) {
    if (visitor == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_each_id(world.NativeHandle(), meshRendererComponentId);
    while (ecs_each_next(&it)) {
        const auto* renderers = SceneComponentIterationAccess::Field<MeshRendererComponent>(it, 0);
        if (renderers == nullptr) {
            continue;
        }

        for (int32_t i = 0; i < it.count; ++i) {
            const SceneEntity entity{ it.entities[i] };
            const auto* transform = SceneComponentIterationAccess::TryGet<TransformComponent>(world, entity, transformComponentId);
            if (transform != nullptr) {
                visitor(entity, *transform, renderers[i], context);
            }
        }
    }
}

void SceneComponentIteration::ForEachVisibleMeshRenderer(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t visibilityComponentId,
    std::uint64_t meshRendererComponentId,
    MeshRendererVisitor visitor,
    void* context) {
    if (visitor == nullptr) {
        return;
    }

    ecs_iter_t it = ecs_each_id(world.NativeHandle(), meshRendererComponentId);
    while (ecs_each_next(&it)) {
        const auto* renderers = SceneComponentIterationAccess::Field<MeshRendererComponent>(it, 0);
        if (renderers == nullptr) {
            continue;
        }

        for (int32_t i = 0; i < it.count; ++i) {
            const SceneEntity entity{ it.entities[i] };
            const auto* transform = SceneComponentIterationAccess::TryGet<TransformComponent>(world, entity, transformComponentId);
            if (transform == nullptr) {
                continue;
            }

            const auto* visibility = SceneComponentIterationAccess::TryGet<VisibilityComponent>(world, entity, visibilityComponentId);
            if (visibility == nullptr || visibility->visible) {
                visitor(entity, *transform, renderers[i], context);
            }
        }
    }
}

} // namespace kb::scene
