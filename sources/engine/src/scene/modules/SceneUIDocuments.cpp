#include "engine/scene/SceneUIDocuments.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneUIDocumentService.hpp"

namespace kb::scene {

SceneUIDocumentComponentQueries::SceneUIDocumentComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneUIDocumentComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasUIDocument(scene_, entity); }
const UIDocumentComponent* SceneUIDocumentComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetUIDocument(scene_, entity); }

SceneUIDocumentComponents::SceneUIDocumentComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneUIDocumentComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasUIDocument(scene_, entity); }
const UIDocumentComponent* SceneUIDocumentComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetUIDocument(scene_, entity); }
UIDocumentComponent* SceneUIDocumentComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetUIDocument(scene_, entity); }
void SceneUIDocumentComponents::Set(SceneEntity entity, const UIDocumentComponent& document) { SceneComponentMutationService::SetUIDocument(scene_, entity, document); }
void SceneUIDocumentComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveUIDocument(scene_, entity); }
void SceneUIDocumentComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkUIDocumentModified(scene_, entity); }

SceneUIDocumentQueries::SceneUIDocumentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneUIDocumentQueries::Exists(SceneEntity entity) const noexcept { return SceneUIDocumentService::Exists(scene_, entity); }
std::uint64_t SceneUIDocumentQueries::Asset(SceneEntity entity) const noexcept { return SceneUIDocumentService::Asset(scene_, entity); }
UIElementId SceneUIDocumentQueries::Root(SceneEntity entity) const noexcept { return SceneUIDocumentService::Root(scene_, entity); }
bool SceneUIDocumentQueries::HasElement(SceneEntity entity, UIElementId element) const noexcept { return SceneUIDocumentService::HasElement(scene_, entity, element); }
bool SceneUIDocumentQueries::Visible(SceneEntity entity, UIElementId element) const noexcept { return SceneUIDocumentService::Visible(scene_, entity, element); }
std::optional<UIControlState> SceneUIDocumentQueries::Control(SceneEntity entity, UIElementId element) const { return SceneUIDocumentService::Control(scene_, entity, element); }
std::optional<UIElementId> SceneUIDocumentQueries::Find(SceneEntity entity, std::string_view name) const noexcept { return SceneUIDocumentService::Find(scene_, entity, name); }
bool SceneUIDocumentQueries::StyleIsResolved(SceneEntity entity) const noexcept { return SceneUIDocumentService::StyleIsResolved(scene_, entity); }
std::size_t SceneUIDocumentQueries::ElementCount(SceneEntity entity) const noexcept { return SceneUIDocumentService::ElementCount(scene_, entity); }

SceneUIDocuments::SceneUIDocuments(Scene& scene) noexcept : scene_(scene) {}
bool SceneUIDocuments::Exists(SceneEntity entity) const noexcept { return SceneUIDocumentService::Exists(scene_, entity); }
std::uint64_t SceneUIDocuments::Asset(SceneEntity entity) const noexcept { return SceneUIDocumentService::Asset(scene_, entity); }
UIElementId SceneUIDocuments::Root(SceneEntity entity) const noexcept { return SceneUIDocumentService::Root(scene_, entity); }
bool SceneUIDocuments::HasElement(SceneEntity entity, UIElementId element) const noexcept { return SceneUIDocumentService::HasElement(scene_, entity, element); }
bool SceneUIDocuments::Visible(SceneEntity entity, UIElementId element) const noexcept { return SceneUIDocumentService::Visible(scene_, entity, element); }
std::optional<UIControlState> SceneUIDocuments::Control(SceneEntity entity, UIElementId element) const { return SceneUIDocumentService::Control(scene_, entity, element); }
std::optional<UIElementId> SceneUIDocuments::Find(SceneEntity entity, std::string_view name) const noexcept { return SceneUIDocumentService::Find(scene_, entity, name); }
bool SceneUIDocuments::StyleIsResolved(SceneEntity entity) const noexcept { return SceneUIDocumentService::StyleIsResolved(scene_, entity); }
std::size_t SceneUIDocuments::ElementCount(SceneEntity entity) const noexcept { return SceneUIDocumentService::ElementCount(scene_, entity); }
std::optional<UIElementId> SceneUIDocuments::QueueCreate(SceneEntity entity, const UIRuntimeElementDesc& desc) { return SceneUIDocumentService::QueueCreate(scene_, entity, desc); }
bool SceneUIDocuments::QueueDestroy(SceneEntity entity, UIElementId element) noexcept { return SceneUIDocumentService::QueueDestroy(scene_, entity, element); }
bool SceneUIDocuments::QueueShow(SceneEntity entity, UIElementId element) noexcept { return SceneUIDocumentService::QueueVisibility(scene_, entity, element, true); }
bool SceneUIDocuments::QueueHide(SceneEntity entity, UIElementId element) noexcept { return SceneUIDocumentService::QueueVisibility(scene_, entity, element, false); }
bool SceneUIDocuments::QueueSetControl(SceneEntity entity, UIElementId element, const UIControlState& control) { return SceneUIDocumentService::QueueSetControl(scene_, entity, element, control); }
bool SceneUIDocuments::QueueEvent(SceneEntity entity, const UIRuntimeEvent& event) { return SceneUIDocumentService::QueueEvent(scene_, entity, event); }
std::vector<UIRuntimeEventRecord> SceneUIDocuments::DrainEvents() { return SceneUIDocumentService::DrainEvents(scene_); }

} // namespace kb::scene
