#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneRegionPortalQueries.hpp"

#include "engine/scene/SceneRegionShapeQueries.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentMutationService.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"

namespace kb::scene {

bool SceneComponentQueryService::HasRegionPortal(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) && SceneAccess::State(scene).componentStorage.RegionPortals().Has(entity); }
const SceneRegionPortalComponent* SceneComponentQueryService::TryGetRegionPortal(const Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.RegionPortals().TryGet(entity) : nullptr; }
SceneRegionPortalComponent* SceneComponentMutationService::TryGetRegionPortal(Scene& scene, SceneEntity entity) noexcept { return SceneEntityService::IsAlive(scene, entity) ? SceneAccess::State(scene).componentStorage.RegionPortals().TryGet(entity) : nullptr; }
void SceneComponentMutationService::SetRegionPortal(Scene& scene, SceneEntity entity, const SceneRegionPortalComponent& component) { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.RegionPortals().Set(entity, component); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::RemoveRegionPortal(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.RegionPortals().Remove(entity); MarkScenePrefabNodeDirty(state, entity); } }
void SceneComponentMutationService::MarkRegionPortalModified(Scene& scene, SceneEntity entity) noexcept { if (SceneEntityService::IsAlive(scene, entity)) { SceneState& state = SceneAccess::State(scene); state.componentStorage.RegionPortals().MarkModified(entity); MarkScenePrefabNodeDirty(state, entity); } }

SceneRegionPortalComponentQueries::SceneRegionPortalComponentQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneRegionPortalComponentQueries::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasRegionPortal(scene_, entity); }
const SceneRegionPortalComponent* SceneRegionPortalComponentQueries::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetRegionPortal(scene_, entity); }
SceneRegionPortalComponents::SceneRegionPortalComponents(Scene& scene) noexcept : scene_(scene) {}
bool SceneRegionPortalComponents::Has(SceneEntity entity) const noexcept { return SceneComponentQueryService::HasRegionPortal(scene_, entity); }
const SceneRegionPortalComponent* SceneRegionPortalComponents::TryGet(SceneEntity entity) const noexcept { return SceneComponentQueryService::TryGetRegionPortal(scene_, entity); }
SceneRegionPortalComponent* SceneRegionPortalComponents::TryGet(SceneEntity entity) noexcept { return SceneComponentMutationService::TryGetRegionPortal(scene_, entity); }
void SceneRegionPortalComponents::Set(SceneEntity entity, const SceneRegionPortalComponent& component) { SceneComponentMutationService::SetRegionPortal(scene_, entity, component); }
void SceneRegionPortalComponents::Remove(SceneEntity entity) noexcept { SceneComponentMutationService::RemoveRegionPortal(scene_, entity); }
void SceneRegionPortalComponents::MarkModified(SceneEntity entity) noexcept { SceneComponentMutationService::MarkRegionPortalModified(scene_, entity); }

bool SceneRegionPortalContains(const Scene& scene, SceneEntity portal, kb::math::Vec3 worldPoint) noexcept {
    const SceneRegionPortalComponent* component = scene.Components().RegionPortals().TryGet(portal);
    return component != nullptr && component->enabled && IsSceneRegionPortalComponentValid(*component) && SceneRegionShapeContains(scene, portal, worldPoint);
}

bool SceneRegionPortalAllows(const Scene& scene, SceneEntity portal, SceneEntity sourceCell, SceneEntity targetCell, RegionPortalPurpose purpose) noexcept {
    const SceneRegionPortalComponent* component = scene.Components().RegionPortals().TryGet(portal);
    const RegionPortalPurposeMask purposeMask = static_cast<RegionPortalPurposeMask>(purpose);
    return component != nullptr && component->enabled && IsSceneRegionPortalComponentValid(*component) &&
        scene.Components().VisibilityCells().Has(component->sourceCell) &&
        scene.Components().VisibilityCells().Has(component->targetCell) &&
        component->sourceCell == sourceCell && component->targetCell == targetCell &&
        (component->purposes & purposeMask) != 0U;
}

} // namespace kb::scene
