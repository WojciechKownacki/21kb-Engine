#include "scene/prefab/ScenePrefabNestedNodeMapper.hpp"

#include "scene/prefab/ScenePrefabNestedOverrideApplicator.hpp"

#include <span>
#include <utility>

namespace kb::scene {

ScenePrefabNestedNodeMapping ScenePrefabNestedNodeMapper::Append(ScenePrefab& output, const ScenePrefab& source, const ScenePrefab& nestedPrefab, const ScenePrefabNodeDesc& overlayRoot, const std::vector<std::uint32_t>& sourceSubtree, std::uint32_t outputParent) {
    ScenePrefabNestedNodeMapping mapping{
        .sourceSubtree = sourceSubtree,
        .sourceToOutput = std::vector<std::uint32_t>(source.NodeCount(), kScenePrefabUnmappedNode),
        .nestedNodeCount = nestedPrefab.NodeCount(),
    };
    std::vector<std::uint32_t> nestedToOutput(nestedPrefab.NodeCount(), kScenePrefabUnmappedNode);

    const std::span<const ScenePrefabNodeDesc> nestedNodes = nestedPrefab.Nodes();
    for (std::uint32_t nestedIndex = 0; nestedIndex < static_cast<std::uint32_t>(nestedNodes.size()); ++nestedIndex) {
        ScenePrefabNodeDesc node = nestedNodes[nestedIndex];
        ScenePrefabNestedOverrideApplicator::Apply(node, nestedIndex, nestedNodes[nestedIndex].stableId, overlayRoot);
        node.stableId = ScenePrefabNodeDesc::InvalidStableId;
        if (nestedIndex == 0) {
            node.nestedPrefabGuid = overlayRoot.nestedPrefabGuid;
            node.nestedPrefabOverrides = overlayRoot.nestedPrefabOverrides;
        }

        const std::uint32_t nestedParent = nestedNodes[nestedIndex].parentNode;
        node.parentNode = nestedParent == ScenePrefabNodeDesc::NoParent ? outputParent : nestedToOutput[nestedParent];
        nestedToOutput[nestedIndex] = output.AddNode(std::move(node));
        if (nestedIndex < mapping.sourceSubtree.size()) {
            mapping.sourceToOutput[mapping.sourceSubtree[nestedIndex]] = nestedToOutput[nestedIndex];
        }
    }

    // Nodes copied from a nested prefab receive fresh stable ids in `output`.
    // Remap every prefab-local joint reference to that new id before the
    // composed prefab is validated or instantiated.
    for (std::uint32_t nestedIndex = 0; nestedIndex < static_cast<std::uint32_t>(nestedNodes.size()); ++nestedIndex) {
        ScenePrefabNodeDesc* outputNode = output.TryGetMutableNode(nestedToOutput[nestedIndex]);
        if (outputNode == nullptr) {
            continue;
        }
        if (outputNode->components.joint.has_value()) {
            ScenePrefabJointComponent& joint = *outputNode->components.joint;
            if (joint.connectedNodeStableId != ScenePrefabJointComponent::InvalidConnectedNodeStableId) {
                const std::uint32_t targetNestedIndex = nestedPrefab.FindNodeIndexByStableId(joint.connectedNodeStableId);
                if (targetNestedIndex != ScenePrefabNodeDesc::NoParent && targetNestedIndex < nestedToOutput.size()) {
                    const ScenePrefabNodeDesc* targetNode = output.TryGetNode(nestedToOutput[targetNestedIndex]);
                    if (targetNode != nullptr) joint.connectedNodeStableId = targetNode->stableId;
                }
            }
        }
        if (outputNode->components.regionPortal.has_value()) {
            ScenePrefabRegionPortalComponent& portal = *outputNode->components.regionPortal;
            const std::uint32_t sourceIndex = nestedPrefab.FindNodeIndexByStableId(portal.sourceCellNodeStableId);
            const std::uint32_t targetIndex = nestedPrefab.FindNodeIndexByStableId(portal.targetCellNodeStableId);
            if (sourceIndex == ScenePrefabNodeDesc::NoParent || targetIndex == ScenePrefabNodeDesc::NoParent || sourceIndex >= nestedToOutput.size() || targetIndex >= nestedToOutput.size()) continue;
            const ScenePrefabNodeDesc* sourceNode = output.TryGetNode(nestedToOutput[sourceIndex]);
            const ScenePrefabNodeDesc* targetNode = output.TryGetNode(nestedToOutput[targetIndex]);
            if (sourceNode == nullptr || targetNode == nullptr) continue;
            portal.sourceCellNodeStableId = sourceNode->stableId;
            portal.targetCellNodeStableId = targetNode->stableId;
        }
        if (outputNode->components.lensEcho.has_value()) {
            ScenePrefabLensEchoComponent& echo = *outputNode->components.lensEcho;
            if (echo.sourceNodeStableId == ScenePrefabLensEchoComponent::InvalidSourceNodeStableId) continue;
            const std::uint32_t sourceIndex = nestedPrefab.FindNodeIndexByStableId(echo.sourceNodeStableId);
            if (sourceIndex == ScenePrefabNodeDesc::NoParent || sourceIndex >= nestedToOutput.size()) continue;
            const ScenePrefabNodeDesc* sourceNode = output.TryGetNode(nestedToOutput[sourceIndex]);
            if (sourceNode != nullptr) echo.sourceNodeStableId = sourceNode->stableId;
        }
    }

    return mapping;
}

} // namespace kb::scene
