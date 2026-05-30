#include "engine/scene/SceneTransforms.hpp"

#include "scene/SceneIterationService.hpp"
#include "scene/SceneTransformService.hpp"

namespace kb::scene {

SceneTransformQueries::SceneTransformQueries(const Scene& scene) noexcept
    : scene_(scene) {}

TransformComponent SceneTransformQueries::Get(SceneObject object) const {
    return SceneTransformService::Get(scene_, object);
}

TransformComponent SceneTransformQueries::Get(SceneEntity entity) const {
    return SceneTransformService::Get(scene_, entity);
}

const TransformComponent* SceneTransformQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneTransformService::TryGet(scene_, entity);
}

void SceneTransformQueries::ForEach(ConstTransformVisitor visitor, void* context) const {
    SceneIterationService::ForEachTransform(scene_, visitor, context);
}

SceneTransforms::SceneTransforms(Scene& scene) noexcept
    : scene_(scene) {}

TransformComponent SceneTransforms::Get(SceneObject object) const {
    return SceneTransformService::Get(scene_, object);
}

TransformComponent SceneTransforms::Get(SceneEntity entity) const {
    return SceneTransformService::Get(scene_, entity);
}

const TransformComponent* SceneTransforms::TryGet(SceneEntity entity) const noexcept {
    return SceneTransformService::TryGet(scene_, entity);
}

TransformComponent* SceneTransforms::TryGet(SceneEntity entity) noexcept {
    return SceneTransformService::TryGet(scene_, entity);
}

void SceneTransforms::Set(SceneObject object, const TransformComponent& transform) {
    SceneTransformService::Set(scene_, object, transform);
}

void SceneTransforms::Set(SceneEntity entity, const TransformComponent& transform) {
    SceneTransformService::Set(scene_, entity, transform);
}

void SceneTransforms::MarkModified(SceneEntity entity) noexcept {
    SceneTransformService::MarkModified(scene_, entity);
}

void SceneTransforms::ForEach(ConstTransformVisitor visitor, void* context) const {
    SceneIterationService::ForEachTransform(scene_, visitor, context);
}

void SceneTransforms::ForEachMutable(MutableTransformVisitor visitor, void* context) {
    SceneIterationService::ForEachMutableTransform(scene_, visitor, context);
}

} // namespace kb::scene
