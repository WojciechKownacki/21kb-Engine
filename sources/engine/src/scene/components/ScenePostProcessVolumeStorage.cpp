#include "scene/components/ScenePostProcessVolumeComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

ScenePostProcessVolumeComponentStore::ScenePostProcessVolumeComponentStore(kb::ecs::World& world, std::uint64_t) noexcept : world_(&world) {}
bool ScenePostProcessVolumeComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<PostProcessVolumeComponent>(world_, entity);
}
const PostProcessVolumeComponent* ScenePostProcessVolumeComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<PostProcessVolumeComponent>(world_, entity);
}
PostProcessVolumeComponent* ScenePostProcessVolumeComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<PostProcessVolumeComponent>(world_, entity);
}
void ScenePostProcessVolumeComponentStore::Set(SceneEntity entity, const PostProcessVolumeComponent& component) {
    SceneComponentStorageAccess::Set<PostProcessVolumeComponent>(world_, entity, component);
}
void ScenePostProcessVolumeComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<PostProcessVolumeComponent>(world_, entity);
}
void ScenePostProcessVolumeComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<PostProcessVolumeComponent>(world_, entity);
}

} // namespace kb::scene
