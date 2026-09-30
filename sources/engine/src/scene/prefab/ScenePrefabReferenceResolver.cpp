#include "scene/prefab/ScenePrefabReferenceResolver.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/UIEntityReferences.hpp"
#include <stdexcept>

namespace kb::scene {
void ScenePrefabReferenceResolver::Apply(Scene& scene, const ScenePrefabNodeDesc& node,
    SceneEntity owner, const EntityMap& entities) {
    const auto resolve = [&entities](std::uint64_t id) {
        if (id == 0U) return SceneEntity{};
        const auto found = entities.find(id);
        if (found == entities.end()) throw std::invalid_argument("Prefab reference target is missing");
        return found->second;
    };
    const auto& components = node.components;
    if (components.joint) {
        const auto& joint = *components.joint;
        scene.Components().Joints().Set(owner, JointComponent{
            .type = joint.type, .connectedEntity = resolve(joint.connectedNodeStableId),
            .anchor = joint.anchor, .connectedAnchor = joint.connectedAnchor, .axis = joint.axis,
            .minLimit = joint.minLimit, .maxLimit = joint.maxLimit, .enableLimit = joint.enableLimit});
    }
    if (components.regionPortal) {
        const auto& portal = *components.regionPortal;
        scene.Components().RegionPortals().Set(owner, SceneRegionPortalComponent{
            .sourceCell = resolve(portal.sourceCellNodeStableId), .targetCell = resolve(portal.targetCellNodeStableId),
            .purposes = portal.purposes, .enabled = portal.enabled});
    }
    if (components.lensEcho) {
        const auto& echo = *components.lensEcho;
        scene.Components().LensEchoes().Set(owner, LensEchoComponent{
            .sourceEntityId = resolve(echo.sourceNodeStableId).Id(), .profileMaterialAssetId = echo.profileMaterialAssetId,
            .intensity = echo.intensity, .size = echo.size, .layer = echo.layer,
            .occlusionRule = echo.occlusionRule, .enabled = echo.enabled});
    }
    if (!components.ui.Empty()) {
        auto ui = components.ui;
        ForEachUIEntityReference(ui, [&resolve](std::uint64_t& reference) {
            if (reference != 0U) reference = resolve(reference).Id();
        });
        ApplySceneUIComponents(scene.Components().UI(), owner, ui);
    }
}
}
