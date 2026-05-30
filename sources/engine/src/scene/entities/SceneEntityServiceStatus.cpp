#include "scene/SceneEntityService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {

bool SceneEntityService::IsAlive(const Scene& scene, SceneObject object) noexcept {
    return SceneAccess::BelongsTo(scene, object) && IsAlive(scene, object.Entity());
}

bool SceneEntityService::IsAlive(const Scene& scene, SceneEntity entity) noexcept {
    return SceneAccess::State(scene).world.IsAlive(entity);
}

} // namespace kb::scene
