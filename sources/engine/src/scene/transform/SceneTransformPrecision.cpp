#include "scene/transform/SceneTransformPrecision.hpp"

#include "scene/SceneState.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/transform/SceneTransformResiduals.hpp"

namespace kb::scene {

Vec3 SceneTransformPrecision::LocalResidual(const SceneState& state, SceneEntity entity, const TransformComponent& transform) noexcept {
    const SceneTransformResidual* residual = state.transformResiduals.Find(entity);
    return residual == nullptr ? Vec3{} : FittingResidual(transform.localPosition, residual->local);
}

void SceneTransformPrecision::StoreLocalResidual(SceneState& state, SceneEntity entity, Vec3 residual) {
    SceneTransformResiduals& residuals = state.transformResiduals;
    if (IsZero(residual) && residuals.Find(entity) == nullptr) return;
    residuals.Reserve(state.denseHierarchyParents.size());
    residuals.Acquire(entity).local = residual;
}

kb::math::DVec3 SceneTransformPrecision::LocalTranslation(const SceneState& state, SceneEntity entity, const TransformComponent& transform) noexcept {
    return state.transformResiduals.Local(entity, transform);
}

kb::math::DVec3 SceneTransformPrecision::WorldTranslation(const SceneState& state, SceneEntity entity, const TransformComponent& transform) noexcept {
    if (state.transformResiduals.Empty()) return kb::math::ToDVec3(transform.worldPosition);
    return state.transformResiduals.World(entity, transform, SceneHierarchyCache::Parent(state, entity).IsValid());
}

} // namespace kb::scene
