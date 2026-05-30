#include "scene/components/SceneMeshRendererIterationDispatcher.hpp"

#include "engine/scene/VisibilityComponent.hpp"
#include "scene/components/SceneComponentIterationAccess.hpp"

#include <flecs.h>

namespace kb::scene {
namespace {

[[nodiscard]] bool IsVisible(const kb::ecs::World& world, SceneEntity entity, std::uint64_t visibilityComponentId) {
    const auto* visibility = SceneComponentIterationAccess::TryGet<VisibilityComponent>(world, entity, visibilityComponentId);
    return visibility == nullptr || visibility->visible;
}

} // namespace

void SceneMeshRendererIterationDispatcher::ForEach(
    const kb::ecs::World& world,
    std::uint64_t transformComponentId,
    std::uint64_t visibilityComponentId,
    std::uint64_t meshRendererComponentId,
    bool visibleOnly,
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
            if (transform == nullptr || (visibleOnly && !IsVisible(world, entity, visibilityComponentId))) {
                continue;
            }
            visitor(entity, *transform, renderers[i], context);
        }
    }
}

} // namespace kb::scene
