#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneGuideCurveComponentQueries::SceneGuideCurveComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneGuideCurveComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasGuideCurve(scene_, entity); }
const GuideCurveComponent* SceneGuideCurveComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetGuideCurve(scene_, entity); }
SceneGuideCurveComponents::SceneGuideCurveComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneGuideCurveComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasGuideCurve(scene_, entity); }
const GuideCurveComponent* SceneGuideCurveComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetGuideCurve(scene_, entity); }
GuideCurveComponent* SceneGuideCurveComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetGuideCurve(scene_, entity); }
void SceneGuideCurveComponents::Set(SceneEntity entity, const GuideCurveComponent& curve) { SceneComponentMutationService::SetGuideCurve(scene_, entity, curve); }
void SceneGuideCurveComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveGuideCurve(scene_, entity); }
void SceneGuideCurveComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkGuideCurveModified(scene_, entity); }

} // namespace kb::scene
