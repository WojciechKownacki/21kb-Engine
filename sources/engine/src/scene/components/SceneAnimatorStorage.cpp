#include "scene/components/SceneAnimatorComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneAnimatorComponentStore::SceneAnimatorComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) { static_cast<void>(componentId); }

bool SceneAnimatorComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<Animator>(world_, entity);
}
const Animator* SceneAnimatorComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<Animator>(world_, entity);
}
Animator* SceneAnimatorComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<Animator>(world_, entity);
}
void SceneAnimatorComponentStore::Set(SceneEntity entity, const Animator& animator) {
    SceneComponentStorageAccess::Set<Animator>(world_, entity, animator);
}
void SceneAnimatorComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<Animator>(world_, entity);
}
void SceneAnimatorComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<Animator>(world_, entity);
}

} // namespace kb::scene
