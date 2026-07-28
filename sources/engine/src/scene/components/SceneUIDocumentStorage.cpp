#include "scene/components/SceneUIDocumentComponentStore.hpp"

#include "scene/components/SceneComponentStorageAccess.hpp"

namespace kb::scene {

SceneUIDocumentComponentStore::SceneUIDocumentComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept
    : world_(&world) { static_cast<void>(componentId); }
bool SceneUIDocumentComponentStore::Has(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::Has<UIDocumentComponent>(world_, entity); }
const UIDocumentComponent* SceneUIDocumentComponentStore::TryGet(SceneEntity entity) const noexcept { return SceneComponentStorageAccess::TryGet<UIDocumentComponent>(world_, entity); }
UIDocumentComponent* SceneUIDocumentComponentStore::TryGet(SceneEntity entity) noexcept { return SceneComponentStorageAccess::TryGetMutable<UIDocumentComponent>(world_, entity); }
void SceneUIDocumentComponentStore::Set(SceneEntity entity, const UIDocumentComponent& document) { SceneComponentStorageAccess::Set<UIDocumentComponent>(world_, entity, document); }
void SceneUIDocumentComponentStore::Remove(SceneEntity entity) noexcept { SceneComponentStorageAccess::Remove<UIDocumentComponent>(world_, entity); }
void SceneUIDocumentComponentStore::MarkModified(SceneEntity entity) noexcept { SceneComponentStorageAccess::MarkModified<UIDocumentComponent>(world_, entity); }

} // namespace kb::scene
