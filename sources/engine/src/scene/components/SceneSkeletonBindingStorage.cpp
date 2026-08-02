#include "scene/components/SceneSkeletonBindingComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneSkeletonBindingComponentStore::SceneSkeletonBindingComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) { static_cast<void>(componentId); }

bool SceneSkeletonBindingComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<SkeletonBindingComponent>(world_, entity);
}
const SkeletonBindingComponent* SceneSkeletonBindingComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<SkeletonBindingComponent>(world_, entity);
}
SkeletonBindingComponent* SceneSkeletonBindingComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<SkeletonBindingComponent>(world_, entity);
}
void SceneSkeletonBindingComponentStore::Set(SceneEntity entity, const SkeletonBindingComponent& binding) {
    SceneComponentStorageAccess::Set<SkeletonBindingComponent>(world_, entity, binding);
}
void SceneSkeletonBindingComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<SkeletonBindingComponent>(world_, entity);
}
void SceneSkeletonBindingComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<SkeletonBindingComponent>(world_, entity);
}

} // namespace kb::scene
