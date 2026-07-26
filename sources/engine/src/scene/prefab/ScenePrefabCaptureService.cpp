#include "scene/prefab/ScenePrefabCaptureService.hpp"

#include "scene/prefab/ScenePrefabCaptureValidator.hpp"
#include "scene/prefab/ScenePrefabCaptureTraversal.hpp"
#include "scene/prefab/ScenePrefabHierarchyCounter.hpp"

#include <unordered_map>
#include <vector>

namespace kb::scene {

namespace {

void ResolveJointReferences(ScenePrefab& prefab, std::span<const SceneEntity> capturedEntities) {
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    std::unordered_map<SceneEntity::IdType, std::uint64_t> stableNodeIds;
    stableNodeIds.reserve(capturedEntities.size());
    for (std::size_t index = 0U; index < capturedEntities.size() && index < nodes.size(); ++index) {
        stableNodeIds.emplace(capturedEntities[index].Id(), nodes[index].stableId);
    }

    for (std::uint32_t index = 0U; index < static_cast<std::uint32_t>(nodes.size()); ++index) {
        ScenePrefabNodeDesc* node = prefab.TryGetMutableNode(index);
        if (node == nullptr || !node->components.joint.has_value()) {
            continue;
        }
        ScenePrefabJointComponent& joint = *node->components.joint;
        if (joint.connectedNodeStableId == ScenePrefabJointComponent::InvalidConnectedNodeStableId) {
            continue;
        }
        const auto target = stableNodeIds.find(joint.connectedNodeStableId);
        joint.connectedNodeStableId = target == stableNodeIds.end()
            ? ScenePrefabJointComponent::UnresolvedConnectedNodeStableId
            : target->second;
    }
}

} // namespace

ScenePrefab ScenePrefabCaptureService::Capture(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings) {
    return CaptureRoots(scene, std::span<const SceneObject>{ &root, 1U }, settings);
}

ScenePrefab ScenePrefabCaptureService::CaptureRoots(Scene& scene, std::span<const SceneObject> roots, const ScenePrefabCaptureSettings& settings) {
    ScenePrefab prefab;
    std::vector<SceneEntity> capturedEntities;

    for (const SceneObject root : roots) {
        if (!ScenePrefabCaptureValidator::CanCapture(scene, root)) {
            continue;
        }
        prefab.Reserve(prefab.NodeCount() + ScenePrefabHierarchyCounter::Count(root, settings));
        ScenePrefabCaptureTraversal::Append(scene, root, settings, prefab, ScenePrefabNodeDesc::NoParent, capturedEntities);
    }
    ResolveJointReferences(prefab, capturedEntities);
    return prefab;
}

} // namespace kb::scene
