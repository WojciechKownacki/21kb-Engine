#include "scene/components/SceneComponentStorage.hpp"

namespace kb::scene {

SceneComponentStorage::SceneComponentStorage(ecs_world_t* world, const SceneComponentRegistry& components) noexcept
    : world_(world)
    , components_(components) {}

void SceneComponentStorage::SetDefaults(SceneEntity entity, const TransformComponent& transform, const VisibilityComponent& visibility) {
    SetTransform(entity, transform);
    SetVisibility(entity, visibility);
}

} // namespace kb::scene
