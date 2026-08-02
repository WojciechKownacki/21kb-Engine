#include "scene/prefab/ScenePrefabValidator.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace kb::scene {

bool ScenePrefabValidator::IsValid(const ScenePrefab& prefab) noexcept {
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    std::vector<std::uint64_t> stableIds;
    stableIds.reserve(nodes.size());
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes.size()); ++nodeIndex) {
        const std::uint32_t parentNode = nodes[nodeIndex].parentNode;
        if (parentNode != ScenePrefabNodeDesc::NoParent && parentNode >= nodeIndex) {
            return false;
        }
        if (nodes[nodeIndex].stableId == ScenePrefabNodeDesc::InvalidStableId ||
            nodes[nodeIndex].stableId == ScenePrefabJointComponent::UnresolvedConnectedNodeStableId) {
            return false;
        }
        stableIds.push_back(nodes[nodeIndex].stableId);
    }
    std::sort(stableIds.begin(), stableIds.end());
    if (std::adjacent_find(stableIds.begin(), stableIds.end()) != stableIds.end()) {
        return false;
    }
    for (const ScenePrefabNodeDesc& node : nodes) {
        if (node.components.joint.has_value()) {
            const std::uint64_t targetStableId = node.components.joint->connectedNodeStableId;
            if (targetStableId != ScenePrefabJointComponent::InvalidConnectedNodeStableId &&
                (targetStableId == node.stableId || !std::binary_search(stableIds.begin(), stableIds.end(), targetStableId))) return false;
        }
        if (node.components.regionPortal.has_value()) {
            const ScenePrefabRegionPortalComponent& portal = *node.components.regionPortal;
            const bool unconfigured = !portal.enabled &&
                portal.sourceCellNodeStableId == ScenePrefabRegionPortalComponent::InvalidCellNodeStableId &&
                portal.targetCellNodeStableId == ScenePrefabRegionPortalComponent::InvalidCellNodeStableId;
            if (unconfigured) continue;
            if (!IsRegionPortalPurposeMaskValid(portal.purposes) || portal.sourceCellNodeStableId == ScenePrefabRegionPortalComponent::InvalidCellNodeStableId ||
                portal.targetCellNodeStableId == ScenePrefabRegionPortalComponent::InvalidCellNodeStableId || portal.sourceCellNodeStableId == portal.targetCellNodeStableId ||
                portal.sourceCellNodeStableId == node.stableId || portal.targetCellNodeStableId == node.stableId ||
                !std::binary_search(stableIds.begin(), stableIds.end(), portal.sourceCellNodeStableId) ||
                !std::binary_search(stableIds.begin(), stableIds.end(), portal.targetCellNodeStableId)) return false;
        }
        if (node.components.auxFrame.has_value() && !IsAuxFrameComponentPersistable(*node.components.auxFrame)) return false;
        if (node.components.geometrySwarm.has_value() && !IsGeometrySwarmComponentPersistable(*node.components.geometrySwarm)) return false;
        if (node.components.surfaceCast.has_value() && !IsSurfaceCastComponentPersistable(*node.components.surfaceCast)) return false;
        if (node.components.facingPanel.has_value() && !IsFacingPanelComponentPersistable(*node.components.facingPanel)) return false;
        if (node.components.spaceStroke.has_value() && !IsSpaceStrokeComponentPersistable(*node.components.spaceStroke)) return false;
        if (node.components.historyRibbon.has_value() && !IsHistoryRibbonComponentPersistable(*node.components.historyRibbon)) return false;
        if (node.components.deformedGeometry.has_value()) {
            const DrawD3DeformedGeometryComponent& geometry = *node.components.deformedGeometry;
            if (geometry.poseSource.IsValid() || !IsDrawD3DeformedGeometryComponentPersistable(geometry)) return false;
        }
        if (node.components.lensEcho.has_value()) {
            const ScenePrefabLensEchoComponent& echo = *node.components.lensEcho;
            LensEchoComponent validation{ .sourceEntityId = echo.sourceNodeStableId, .profileMaterialAssetId = echo.profileMaterialAssetId,
                .intensity = echo.intensity, .size = echo.size, .layer = echo.layer, .occlusionRule = echo.occlusionRule, .enabled = echo.enabled };
            const bool unconfigured = !echo.enabled && echo.sourceNodeStableId == ScenePrefabLensEchoComponent::InvalidSourceNodeStableId;
            if (!unconfigured && (!IsLensEchoComponentPersistable(validation) || echo.sourceNodeStableId == ScenePrefabLensEchoComponent::InvalidSourceNodeStableId ||
                echo.sourceNodeStableId == node.stableId || !std::binary_search(stableIds.begin(), stableIds.end(), echo.sourceNodeStableId))) return false;
        }
    }
    return true;
}

} // namespace kb::scene
