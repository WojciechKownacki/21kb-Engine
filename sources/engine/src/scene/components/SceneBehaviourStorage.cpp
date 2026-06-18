#include "scene/components/SceneBehaviourComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneBehaviourComponentStore::SceneBehaviourComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world)
    , componentId_(componentId) {}

bool SceneBehaviourComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<BehaviourComponent>(world_, entity);
}

const BehaviourComponent* SceneBehaviourComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<BehaviourComponent>(world_, entity);
}

BehaviourComponent* SceneBehaviourComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<BehaviourComponent>(world_, entity);
}

void SceneBehaviourComponentStore::Set(SceneEntity entity, const BehaviourComponent& behaviour) {
    SceneComponentStorageAccess::Set<BehaviourComponent>(world_, entity, behaviour);
}

void SceneBehaviourComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<BehaviourComponent>(world_, entity);
}

void SceneBehaviourComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<BehaviourComponent>(world_, entity);
}

} // namespace kb::scene
