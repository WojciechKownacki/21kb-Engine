#include "scene/components/SceneLightComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneLightComponentStore::SceneLightComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

bool SceneLightComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, componentId_);
}

const LightComponent* SceneLightComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<LightComponent>(world_, entity, componentId_);
}

LightComponent* SceneLightComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<LightComponent>(world_, entity, componentId_);
}

void SceneLightComponentStore::Set(SceneEntity entity, const LightComponent& light) {
    SceneComponentStorageAccess::Set(world_, entity, componentId_, light);
}

void SceneLightComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, componentId_);
}

void SceneLightComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
