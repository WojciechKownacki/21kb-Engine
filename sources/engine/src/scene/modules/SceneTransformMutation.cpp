#include "engine/scene/SceneTransforms.hpp"

#include "scene/SceneTransformService.hpp"

namespace kb::scene {

void SceneTransforms::Set(SceneObject object, const TransformComponent& transform) {
    SceneTransformService::Set(scene_, object, transform);
}

void SceneTransforms::Set(SceneEntity entity, const TransformComponent& transform) {
    SceneTransformService::Set(scene_, entity, transform);
}

void SceneTransforms::MarkModified(SceneEntity entity) noexcept {
    SceneTransformService::MarkModified(scene_, entity);
}

void SceneTransforms::MarkModified(std::span<const SceneEntity> entities) noexcept {
    SceneTransformService::MarkModified(scene_, entities);
}

} // namespace kb::scene
