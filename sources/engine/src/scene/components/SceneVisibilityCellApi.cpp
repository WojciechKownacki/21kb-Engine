#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneVisibilityCellQueries.hpp"

#include "engine/scene/SceneRegionShapeQueries.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasVisibilityCell(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.VisibilityCells().Has(entity); }
const VisibilityCellComponent* SceneComponentQueryService::TryGetVisibilityCell(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.VisibilityCells().TryGet(entity) : nullptr; }
VisibilityCellComponent* SceneComponentMutationService::TryGetVisibilityCell(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.VisibilityCells().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetVisibilityCell(Scene& scene, SceneEntity entity, const VisibilityCellComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.VisibilityCells().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveVisibilityCell(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.VisibilityCells().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkVisibilityCellModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.VisibilityCells().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneVisibilityCellComponentQueries::SceneVisibilityCellComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneVisibilityCellComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasVisibilityCell(scene_, entity); }
const VisibilityCellComponent* SceneVisibilityCellComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetVisibilityCell(scene_, entity); }
SceneVisibilityCellComponents::SceneVisibilityCellComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneVisibilityCellComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasVisibilityCell(scene_, entity); }
const VisibilityCellComponent* SceneVisibilityCellComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetVisibilityCell(scene_, entity); }
VisibilityCellComponent* SceneVisibilityCellComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetVisibilityCell(scene_, entity); }
void SceneVisibilityCellComponents::Set(SceneEntity entity, const VisibilityCellComponent& component) { SceneComponentMutationService::SetVisibilityCell(scene_, entity, component); }
void SceneVisibilityCellComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveVisibilityCell(scene_, entity); }
void SceneVisibilityCellComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkVisibilityCellModified(scene_, entity); }

bool SceneVisibilityCellContains(const Scene& scene, SceneEntity entity, kb::math::Vec3 worldPoint) noexcept {
    const VisibilityCellComponent* cell = scene.Components().VisibilityCells().TryGet(entity);
    return cell != nullptr && cell->enabled && IsVisibilityCellComponentValid(*cell) && SceneRegionShapeContains(scene, entity, worldPoint);
}

bool SceneVisibilityCellApplies(const VisibilityCellComponent& cell, std::uint32_t membershipMask) noexcept {
    return cell.enabled && IsVisibilityCellComponentValid(cell) && (cell.membershipMask & membershipMask) != 0U;
}

} // namespace kb::scene
