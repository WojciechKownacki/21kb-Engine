#include "engine/scene/SceneAIBrainComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneAIBrainComponentQueries::SceneAIBrainComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneAIBrainComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasAIBrain(scene_, entity); }
const AIBrainComponent* SceneAIBrainComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetAIBrain(scene_, entity); }

SceneAIBrainComponents::SceneAIBrainComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneAIBrainComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasAIBrain(scene_, entity); }
const AIBrainComponent* SceneAIBrainComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetAIBrain(scene_, entity); }
AIBrainComponent* SceneAIBrainComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetAIBrain(scene_, entity); }
bool SceneAIBrainComponents::Set(SceneEntity entity, const AIBrainComponent& aiBrain) { return SceneComponentMutationService::SetAIBrain(scene_, entity, aiBrain); }
void SceneAIBrainComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveAIBrain(scene_, entity); }
void SceneAIBrainComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkAIBrainModified(scene_, entity); }

} // namespace kb::scene
