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
        ScenePrefabNestedOverrideApplicator::Apply(node, nestedIndex, overlayRoot);
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

    return mapping;
}

} // namespace kb::scene
