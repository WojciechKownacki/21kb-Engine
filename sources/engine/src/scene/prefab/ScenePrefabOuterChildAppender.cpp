#include "scene/prefab/ScenePrefabOuterChildAppender.hpp"

#include <utility>

namespace kb::scene {

void ScenePrefabOuterChildAppender::Append(const ScenePrefab& source, ScenePrefab& output, ScenePrefabNestedNodeMapping& mapping, std::uint32_t outputParent) {
    for (std::size_t index = mapping.nestedNodeCount; index < mapping.sourceSubtree.size(); ++index) {
        const std::uint32_t sourceIndex = mapping.sourceSubtree[index];
        ScenePrefabNodeDesc node = source.Nodes()[sourceIndex];
        const std::uint32_t sourceParent = node.parentNode;
        node.parentNode = sourceParent != ScenePrefabNodeDesc::NoParent
                && sourceParent < mapping.sourceToOutput.size()
                && mapping.sourceToOutput[sourceParent] != kScenePrefabUnmappedNode
            ? mapping.sourceToOutput[sourceParent]
            : outputParent;
        mapping.sourceToOutput[sourceIndex] = output.AddNode(std::move(node));
    }
}

} // namespace kb::scene
