#include "scene/components/SceneSocketAttachmentComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneSocketAttachmentComponentStore::SceneSocketAttachmentComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) { static_cast<void>(componentId); }

bool SceneSocketAttachmentComponentStore::Has(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::Has<SocketAttachmentComponent>(world_, entity);
}
const SocketAttachmentComponent* SceneSocketAttachmentComponentStore::TryGet(SceneEntity entity) const noexcept {
    return SceneComponentStorageAccess::TryGet<SocketAttachmentComponent>(world_, entity);
}
SocketAttachmentComponent* SceneSocketAttachmentComponentStore::TryGet(SceneEntity entity) noexcept {
    return SceneComponentStorageAccess::TryGetMutable<SocketAttachmentComponent>(world_, entity);
}
void SceneSocketAttachmentComponentStore::Set(SceneEntity entity, const SocketAttachmentComponent& attachment) {
    SceneComponentStorageAccess::Set<SocketAttachmentComponent>(world_, entity, attachment);
}
void SceneSocketAttachmentComponentStore::Remove(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::Remove<SocketAttachmentComponent>(world_, entity);
}
void SceneSocketAttachmentComponentStore::MarkModified(SceneEntity entity) noexcept {
    SceneComponentStorageAccess::MarkModified<SocketAttachmentComponent>(world_, entity);
}

} // namespace kb::scene
