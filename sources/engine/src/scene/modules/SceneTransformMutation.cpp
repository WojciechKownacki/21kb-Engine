#include "engine/scene/SceneTransforms.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include "scene/SceneTransformService.hpp"

namespace kb::scene {

void SceneTransforms::Set(SceneObject object, const TransformComponent& transform) {
    SceneTransformService::Set(scene_, object, transform);
}

void SceneTransforms::Set(SceneEntity entity, const TransformComponent& transform) {
    SceneTransformService::Set(scene_, entity, transform);
}

void SceneTransforms::SetMany(std::span<const SceneEntity> entities, std::span<const TransformComponent> transforms) {
    SceneTransformService::SetMany(scene_, entities, transforms);
}

void SceneTransforms::MarkModified(SceneEntity entity) noexcept {
    SceneTransformService::MarkModified(scene_, entity);
}

void SceneTransforms::MarkModified(std::span<const SceneEntity> entities) noexcept {
    SceneTransformService::MarkModified(scene_, entities);
}

TransformPassStats SceneTransforms::ParallelForEachRoot(std::size_t grainRows, std::span<const kb::ecs::ComponentId> extraComponents, TransformRangeBody body, void* context) {
    return SceneTransformService::ParallelForEachRoot(scene_, grainRows, extraComponents, body, context);
}

const void* SceneTransforms::EcsWorldHandle() const noexcept {
    return &scene_.Runtime().EcsWorld();
}

void SceneTransforms::SetInterpolated(SceneEntity entity, bool interpolated) {
    SceneTransformService::SetInterpolated(scene_, entity, interpolated);
}

bool SceneTransforms::IsInterpolated(SceneEntity entity) const noexcept {
    return SceneTransformService::IsInterpolated(scene_, entity);
}

} // namespace kb::scene
