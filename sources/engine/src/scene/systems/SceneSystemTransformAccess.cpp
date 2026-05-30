#include "engine/scene/SceneSystemTransformAccess.hpp"

#include "scene/SceneEntityService.hpp"
#include "scene/SceneIterationService.hpp"
#include "scene/SceneTransformService.hpp"
#include "scene/systems/SceneSystemMutableTransformIteration.hpp"

namespace kb::scene {

SceneSystemTransformAccess::SceneSystemTransformAccess(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneSystemTransformAccess::IsAlive(SceneEntity entity) const noexcept {
    return SceneEntityService::IsAlive(scene_, entity);
}

TransformComponent SceneSystemTransformAccess::Get(SceneEntity entity) const {
    return SceneTransformService::Get(scene_, entity);
}

const TransformComponent* SceneSystemTransformAccess::TryGet(SceneEntity entity) const noexcept {
    return SceneTransformService::TryGet(scene_, entity);
}

TransformComponent* SceneSystemTransformAccess::TryGet(SceneEntity entity) noexcept {
    return SceneTransformService::TryGet(scene_, entity);
}

void SceneSystemTransformAccess::Set(SceneEntity entity, const TransformComponent& transform) {
    SceneTransformService::Set(scene_, entity, transform);
}

void SceneSystemTransformAccess::MarkModified(SceneEntity entity) noexcept {
    SceneTransformService::MarkModified(scene_, entity);
}

void SceneSystemTransformAccess::ForEach(ConstTransformVisitor visitor, void* context) const {
    SceneIterationService::ForEachTransform(scene_, visitor, context);
}

void SceneSystemTransformAccess::ForEachMutable(MutableTransformVisitor visitor, void* context) {
    SceneSystemMutableTransformIteration::ForEach(scene_, *this, visitor, context);
}

} // namespace kb::scene
