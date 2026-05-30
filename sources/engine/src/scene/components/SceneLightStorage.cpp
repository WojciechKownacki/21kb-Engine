#include "scene/components/SceneComponentStorage.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

bool SceneComponentStorage::HasLight(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, components_.LightComponentId());
}

const LightComponent* SceneComponentStorage::TryGetLight(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<LightComponent>(world_, entity, components_.LightComponentId());
}

LightComponent* SceneComponentStorage::TryGetLight(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<LightComponent>(world_, entity, components_.LightComponentId());
}

void SceneComponentStorage::SetLight(SceneEntity entity, const LightComponent& light) {
    SceneComponentStorageAccess::Set(world_, entity, components_.LightComponentId(), light);
}

void SceneComponentStorage::RemoveLight(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, components_.LightComponentId());
}

void SceneComponentStorage::MarkLightModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, components_.LightComponentId());
}

} // namespace kb::scene
