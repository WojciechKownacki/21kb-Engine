#include "engine/scene/SceneComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneRegionShapeComponentQueries::SceneRegionShapeComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneRegionShapeComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasRegionShape(scene_, entity); }
const RegionShapeComponent* SceneRegionShapeComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetRegionShape(scene_, entity); }

SceneRegionShapeComponents::SceneRegionShapeComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneRegionShapeComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasRegionShape(scene_, entity); }
const RegionShapeComponent* SceneRegionShapeComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetRegionShape(scene_, entity); }
RegionShapeComponent* SceneRegionShapeComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetRegionShape(scene_, entity); }
void SceneRegionShapeComponents::Set(SceneEntity entity, const RegionShapeComponent& shape) { SceneComponentMutationService::SetRegionShape(scene_, entity, shape); }
void SceneRegionShapeComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveRegionShape(scene_, entity); }
void SceneRegionShapeComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkRegionShapeModified(scene_, entity); }

} // namespace kb::scene
