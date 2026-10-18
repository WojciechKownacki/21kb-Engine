#include "engine/scene/SceneSocketAttachmentComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneSocketAttachmentComponentQueries::SceneSocketAttachmentComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneSocketAttachmentComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasSocketAttachment(scene_, entity); }
const SocketAttachmentComponent* SceneSocketAttachmentComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetSocketAttachment(scene_, entity); }

SceneSocketAttachmentComponents::SceneSocketAttachmentComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneSocketAttachmentComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasSocketAttachment(scene_, entity); }
const SocketAttachmentComponent* SceneSocketAttachmentComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetSocketAttachment(scene_, entity); }
SocketAttachmentComponent* SceneSocketAttachmentComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetSocketAttachment(scene_, entity); }
bool SceneSocketAttachmentComponents::Set(SceneEntity entity, const SocketAttachmentComponent& attachment) { return SceneComponentMutationService::SetSocketAttachment(scene_, entity, attachment); }
void SceneSocketAttachmentComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveSocketAttachment(scene_, entity); }
void SceneSocketAttachmentComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkSocketAttachmentModified(scene_, entity); }

} // namespace kb::scene
