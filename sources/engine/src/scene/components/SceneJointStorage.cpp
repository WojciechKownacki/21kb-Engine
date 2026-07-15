#include "scene/components/SceneJointComponentStore.hpp"

#include "scene/components/SceneComponentAccess.hpp"
#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneJointComponentStore::SceneJointComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) {
    static_cast<void>(componentId);
}

bool SceneJointComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<JointComponent>(world_, entity);
}

const JointComponent* SceneJointComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<JointComponent>(world_, entity);
}

JointComponent* SceneJointComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<JointComponent>(world_, entity);
}

void SceneJointComponentStore::Set(SceneEntity entity, const JointComponent& joint) {
    SceneComponentStorageAccess::Set<JointComponent>(world_, entity, joint);
}

void SceneJointComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<JointComponent>(world_, entity);
}

void SceneJointComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<JointComponent>(world_, entity);
}

} // namespace kb::scene
