#include "scene/components/SceneLightComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneLightComponentStore::SceneLightComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world)
    , componentId_(componentId) {}

bool SceneLightComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<LightComponent>(world_, entity);
}

const LightComponent* SceneLightComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<LightComponent>(world_, entity);
}

LightComponent* SceneLightComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<LightComponent>(world_, entity);
}

void SceneLightComponentStore::Set(SceneEntity entity, const LightComponent& light) {
    SceneComponentStorageAccess::Set<LightComponent>(world_, entity, light);
}

void SceneLightComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<LightComponent>(world_, entity);
}

void SceneLightComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<LightComponent>(world_, entity);
}

} // namespace kb::scene
