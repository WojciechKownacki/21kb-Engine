#include "scene/components/SceneBehaviourComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneBehaviourComponentStore::SceneBehaviourComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept
    : world_(world)
    , componentId_(componentId) {}

bool SceneBehaviourComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentAccess::Has(world_, entity, componentId_);
}

const BehaviourComponent* SceneBehaviourComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<BehaviourComponent>(world_, entity, componentId_);
}

BehaviourComponent* SceneBehaviourComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<BehaviourComponent>(world_, entity, componentId_);
}

void SceneBehaviourComponentStore::Set(SceneEntity entity, const BehaviourComponent& behaviour) {
    SceneComponentStorageAccess::Set(world_, entity, componentId_, behaviour);
}

void SceneBehaviourComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentAccess::Remove(world_, entity, componentId_);
}

void SceneBehaviourComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentAccess::MarkModified(world_, entity, componentId_);
}

} // namespace kb::scene
