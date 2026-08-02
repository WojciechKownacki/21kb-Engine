#include "engine/scene/SceneDeformedGeometryComponents.hpp"

#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"

namespace kb::scene {

SceneDeformedGeometryComponentQueries::SceneDeformedGeometryComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneDeformedGeometryComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasDeformedGeometry(scene_, entity); }
const DrawD3DeformedGeometryComponent* SceneDeformedGeometryComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetDeformedGeometry(scene_, entity); }
SceneDeformedGeometryComponents::SceneDeformedGeometryComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneDeformedGeometryComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasDeformedGeometry(scene_, entity); }
const DrawD3DeformedGeometryComponent* SceneDeformedGeometryComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetDeformedGeometry(scene_, entity); }
DrawD3DeformedGeometryComponent* SceneDeformedGeometryComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetDeformedGeometry(scene_, entity); }
bool SceneDeformedGeometryComponents::Set(SceneEntity entity, const DrawD3DeformedGeometryComponent& geometry) { return SceneComponentMutationService::SetDeformedGeometry(scene_, entity, geometry); }
void SceneDeformedGeometryComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveDeformedGeometry(scene_, entity); }
void SceneDeformedGeometryComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkDeformedGeometryModified(scene_, entity); }

} // namespace kb::scene
