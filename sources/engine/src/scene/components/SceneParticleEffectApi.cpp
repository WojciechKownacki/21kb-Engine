#include "engine/scene/SceneParticleEffectComponents.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasParticleEffect(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) &&
           SceneAccess::State(scene).componentStorage.ParticleEffects().Has(entity);
}
const ParticleEffectComponent* SceneComponentQueryService::TryGetParticleEffect(const Scene& scene,
                                                                                SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity)
               ? SceneAccess::State(scene).componentStorage.ParticleEffects().TryGet(entity)
               : nullptr;
}
ParticleEffectComponent* SceneComponentMutationService::TryGetParticleEffect(Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity)
               ? SceneAccess::State(scene).componentStorage.ParticleEffects().TryGet(entity)
               : nullptr;
}
void SceneComponentMutationService::SetParticleEffect(Scene& scene, SceneEntity entity,
                                                      const ParticleEffectComponent& component) {
    if (SceneEntityService::IsAlive(scene, entity) && IsParticleEffectComponentPersistable(component)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.ParticleEffects().Set(entity, component);
        MarkScenePrefabNodeDirty(state, entity);
    }
}
void SceneComponentMutationService::RemoveParticleEffect(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.ParticleEffects().Remove(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}
void SceneComponentMutationService::MarkParticleEffectModified(Scene& scene, SceneEntity entity) noexcept {
    if (SceneEntityService::IsAlive(scene, entity)) {
        SceneState& state = SceneAccess::State(scene);
        state.componentStorage.ParticleEffects().MarkModified(entity);
        MarkScenePrefabNodeDirty(state, entity);
    }
}

SceneParticleEffectComponentQueries::SceneParticleEffectComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneParticleEffectComponentQueries::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasParticleEffect(scene_, entity);
}
const ParticleEffectComponent* SceneParticleEffectComponentQueries::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetParticleEffect(scene_, entity);
}
void SceneParticleEffectComponentQueries::ForEach(ParticleEffectVisitor visitor, void* context) const {
    SceneAccess::State(scene_).componentStorage.ParticleEffects().ForEach(visitor, context);
}
SceneParticleEffectComponents::SceneParticleEffectComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneParticleEffectComponents::Has(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::HasParticleEffect(scene_, entity);
}
const ParticleEffectComponent* SceneParticleEffectComponents::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentQueryService::TryGetParticleEffect(scene_, entity);
}
ParticleEffectComponent* SceneParticleEffectComponents::TryGet(SceneEntity entity) noexcept {
    return SceneComponentMutationService::TryGetParticleEffect(scene_, entity);
}
void SceneParticleEffectComponents::Set(SceneEntity entity, const ParticleEffectComponent& component) {
    SceneComponentMutationService::SetParticleEffect(scene_, entity, component);
}
void SceneParticleEffectComponents::Remove(SceneEntity entity) noexcept {
    SceneComponentMutationService::RemoveParticleEffect(scene_, entity);
}
void SceneParticleEffectComponents::MarkModified(SceneEntity entity) noexcept {
    SceneComponentMutationService::MarkParticleEffectModified(scene_, entity);
}

} // namespace kb::scene
