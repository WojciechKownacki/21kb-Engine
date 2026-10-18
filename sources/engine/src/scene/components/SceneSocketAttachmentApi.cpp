#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHistoryService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasSocketAttachment(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.SocketAttachments().Has(entity);
}
const SocketAttachmentComponent* SceneComponentQueryService::TryGetSocketAttachment(const Scene& scene, SceneEntity entity) noexcept {
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.SocketAttachments().TryGet(entity) : nullptr;
}
SocketAttachmentComponent* SceneComponentMutationService::TryGetSocketAttachment(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.SocketAttachments().TryGet(entity) : nullptr;
}
bool SceneComponentMutationService::SetSocketAttachment(Scene& scene, SceneEntity entity, const SocketAttachmentComponent& attachment) {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (!SceneEntityService::IsAlive(scene, entity) || !IsSocketAttachmentComponentValid(attachment)) return false;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.SocketAttachments().Set(entity, attachment);
    MarkScenePrefabNodeDirty(state, entity);
    return true;
}
void SceneComponentMutationService::RemoveSocketAttachment(Scene& scene, SceneEntity entity) noexcept {
    SceneHistoryService::NoteObjectChanging(scene, entity);
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.SocketAttachments().Remove(entity);
    MarkScenePrefabNodeDirty(state, entity);
}
void SceneComponentMutationService::MarkSocketAttachmentModified(Scene& scene, SceneEntity entity) noexcept {
    if (!SceneEntityService::IsAlive(scene, entity)) return;
    SceneState& state = SceneAccess::State(scene);
    state.componentStorage.SocketAttachments().MarkModified(entity);
    MarkScenePrefabNodeDirty(state, entity);
}

} // namespace kb::scene
