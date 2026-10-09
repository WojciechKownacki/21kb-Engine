#include "engine/scene/SceneSystemTransformAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneTransformService.hpp"
#include "scene/transform/SceneTransformPrecision.hpp"
#include "scene/transform/SceneTransformResiduals.hpp"

namespace kb::scene {

kb::math::DVec3 SceneTransformService::LocalTranslation(const Scene& scene, SceneEntity entity, const TransformComponent* transform) noexcept {
    return transform == nullptr ? kb::math::DVec3{} : SceneTransformPrecision::LocalTranslation(SceneAccess::State(scene), entity, *transform);
}

kb::math::DVec3 SceneTransformService::WorldTranslation(const Scene& scene, SceneEntity entity, const TransformComponent* transform) noexcept {
    return transform == nullptr ? kb::math::DVec3{} : SceneTransformPrecision::WorldTranslation(SceneAccess::State(scene), entity, *transform);
}

void SceneTransformService::SetLocalTranslation(Scene& scene, SceneEntity entity, const kb::math::DVec3& translation) {
    const TransformComponent* current = TryGet(static_cast<const Scene&>(scene), entity);
    if (current == nullptr) return;
    TransformComponent value = *current;
    Vec3 residual{};
    SplitTranslation(translation, value.localPosition, residual);
    // The float view goes through the normal write; the residual is stored after it, so an edit recorded by the
    // write sees the translation the entity had before.
    Set(scene, entity, value);
    SceneTransformPrecision::StoreLocalResidual(SceneAccess::State(scene), entity, residual);
}

kb::math::DVec3 SceneTransformQueries::LocalTranslation(SceneEntity entity) const noexcept {
    return SceneTransformService::LocalTranslation(scene_, entity, SceneTransformService::TryGet(scene_, entity));
}

kb::math::DVec3 SceneTransformQueries::WorldTranslation(SceneEntity entity) const noexcept {
    return SceneTransformService::WorldTranslation(scene_, entity, SceneTransformService::TryGet(scene_, entity));
}

kb::math::DVec3 SceneTransformQueries::LocalTranslation(SceneEntity entity, const TransformComponent& transform) const noexcept {
    return SceneTransformService::LocalTranslation(scene_, entity, &transform);
}

kb::math::DVec3 SceneTransformQueries::WorldTranslation(SceneEntity entity, const TransformComponent& transform) const noexcept {
    return SceneTransformService::WorldTranslation(scene_, entity, &transform);
}

kb::math::DVec3 SceneTransforms::LocalTranslation(SceneEntity entity) const noexcept {
    return SceneTransformService::LocalTranslation(scene_, entity, SceneTransformService::TryGet(static_cast<const Scene&>(scene_), entity));
}

kb::math::DVec3 SceneTransforms::WorldTranslation(SceneEntity entity) const noexcept {
    return SceneTransformService::WorldTranslation(scene_, entity, SceneTransformService::TryGet(static_cast<const Scene&>(scene_), entity));
}

kb::math::DVec3 SceneTransforms::LocalTranslation(SceneEntity entity, const TransformComponent& transform) const noexcept {
    return SceneTransformService::LocalTranslation(scene_, entity, &transform);
}

kb::math::DVec3 SceneTransforms::WorldTranslation(SceneEntity entity, const TransformComponent& transform) const noexcept {
    return SceneTransformService::WorldTranslation(scene_, entity, &transform);
}

void SceneTransforms::SetLocalTranslation(SceneEntity entity, const kb::math::DVec3& translation) {
    SceneTransformService::SetLocalTranslation(scene_, entity, translation);
}

kb::math::DVec3 SceneSystemTransformAccess::LocalTranslation(SceneEntity entity) const noexcept {
    return SceneTransformService::LocalTranslation(scene_, entity, SceneTransformService::TryGet(static_cast<const Scene&>(scene_), entity));
}

kb::math::DVec3 SceneSystemTransformAccess::WorldTranslation(SceneEntity entity) const noexcept {
    return SceneTransformService::WorldTranslation(scene_, entity, SceneTransformService::TryGet(static_cast<const Scene&>(scene_), entity));
}

kb::math::DVec3 SceneSystemTransformAccess::LocalTranslation(SceneEntity entity, const TransformComponent& transform) const noexcept {
    return SceneTransformService::LocalTranslation(scene_, entity, &transform);
}

kb::math::DVec3 SceneSystemTransformAccess::WorldTranslation(SceneEntity entity, const TransformComponent& transform) const noexcept {
    return SceneTransformService::WorldTranslation(scene_, entity, &transform);
}

void SceneSystemTransformAccess::SetLocalTranslation(SceneEntity entity, const kb::math::DVec3& translation) {
    SceneTransformService::SetLocalTranslation(scene_, entity, translation);
}

} // namespace kb::scene
