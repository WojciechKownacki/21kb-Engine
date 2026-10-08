#include "scene/prefab/ScenePrefabCaptureService.hpp"

#include "engine/ui/UIEntityReferences.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabCaptureValidator.hpp"
#include "scene/prefab/ScenePrefabCaptureTraversal.hpp"
#include "scene/prefab/ScenePrefabHierarchyCounter.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace kb::scene {

namespace {

void ResolveEntityReferences(ScenePrefab& prefab, std::span<const SceneEntity> capturedEntities) {
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    std::unordered_map<SceneEntity::IdType, std::uint64_t> stableNodeIds;
    stableNodeIds.reserve(capturedEntities.size());
    for (std::size_t index = 0U; index < capturedEntities.size() && index < nodes.size(); ++index) {
        stableNodeIds.emplace(capturedEntities[index].Id(), nodes[index].stableId);
    }

    for (std::uint32_t index = 0U; index < static_cast<std::uint32_t>(nodes.size()); ++index) {
        ScenePrefabNodeDesc* node = prefab.TryGetMutableNode(index);
        if (node == nullptr) {
            continue;
        }
        if (node->components.joint.has_value()) {
            ScenePrefabJointComponent& joint = *node->components.joint;
            if (joint.connectedNodeStableId != ScenePrefabJointComponent::InvalidConnectedNodeStableId) {
                const auto target = stableNodeIds.find(joint.connectedNodeStableId);
                joint.connectedNodeStableId = target == stableNodeIds.end()
                    ? ScenePrefabJointComponent::UnresolvedConnectedNodeStableId
                    : target->second;
            }
        }
        if (node->components.regionPortal.has_value()) {
            ScenePrefabRegionPortalComponent& portal = *node->components.regionPortal;
            if (portal.sourceCellNodeStableId != ScenePrefabRegionPortalComponent::InvalidCellNodeStableId) {
                const auto source = stableNodeIds.find(portal.sourceCellNodeStableId);
                portal.sourceCellNodeStableId = source == stableNodeIds.end() ? ScenePrefabRegionPortalComponent::UnresolvedCellNodeStableId : source->second;
            }
            if (portal.targetCellNodeStableId != ScenePrefabRegionPortalComponent::InvalidCellNodeStableId) {
                const auto targetCell = stableNodeIds.find(portal.targetCellNodeStableId);
                portal.targetCellNodeStableId = targetCell == stableNodeIds.end() ? ScenePrefabRegionPortalComponent::UnresolvedCellNodeStableId : targetCell->second;
            }
        }
        if (node->components.lensEcho.has_value()) {
            ScenePrefabLensEchoComponent& echo = *node->components.lensEcho;
            if (echo.sourceNodeStableId != ScenePrefabLensEchoComponent::InvalidSourceNodeStableId) {
                const auto source = stableNodeIds.find(echo.sourceNodeStableId);
                echo.sourceNodeStableId = source == stableNodeIds.end() ? ScenePrefabLensEchoComponent::UnresolvedSourceNodeStableId : source->second;
            }
        }
        // UI references - navigation links, graphics, scrollbars, a dropdown's widgets - hold live entity
        // ids, which a reload, an undo snapshot or a packaged scene all hand out afresh. Persist them as
        // the target's stable node id. A target outside what is being captured cannot be expressed by
        // this prefab, so that reference is dropped rather than kept as an id naming an unrelated object.
        ForEachUIEntityReference(node->components.ui, [&stableNodeIds](std::uint64_t& reference) {
            if (reference == 0U) return;
            const auto target = stableNodeIds.find(reference);
            reference = target == stableNodeIds.end() ? 0U : target->second;
        });
    }
}

// An instance root records which prefab node each object of its subtree stands for, so instantiating the
// capture again links every object back to its own node, also after renames and moves inside the instance.
void RecordNestedNodeIds(Scene& scene, ScenePrefab& prefab, std::span<const SceneEntity> capturedEntities) {
    const ScenePrefabInstanceRegistry& instances = SceneAccess::State(scene).prefabInstances;
    const std::uint32_t nodeCount = static_cast<std::uint32_t>(std::min(prefab.NodeCount(), capturedEntities.size()));
    for (std::uint32_t root = 0U; root < nodeCount; ++root) {
        if (prefab.Nodes()[root].nestedPrefabGuid.empty()) {
            continue;
        }
        const ScenePrefabInstanceHandle instance = instances.FindRootInstance(SceneAccess::MakeObject(scene, capturedEntities[root]));
        if (!instance.IsValid()) {
            continue;
        }
        // Capture adds a subtree depth first: it is the run of nodes whose parents lie inside it.
        std::vector<std::uint64_t> nodeIds;
        for (std::uint32_t node = root; node < nodeCount; ++node) {
            const std::uint32_t parentNode = prefab.Nodes()[node].parentNode;
            if (node != root && (parentNode < root || parentNode >= node)) {
                break;
            }
            std::uint32_t nodeIndex = 0U;
            std::uint64_t nodeId = ScenePrefabNodeDesc::InvalidStableId;
            const ScenePrefabInstanceHandle owner = instances.FindContainingInstance(SceneAccess::MakeObject(scene, capturedEntities[node]), nodeIndex, nodeId);
            nodeIds.push_back(owner == instance ? nodeId : ScenePrefabNodeDesc::InvalidStableId);
        }
        prefab.TryGetMutableNode(root)->nestedPrefabNodeIds = std::move(nodeIds);
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
    ResolveEntityReferences(prefab, capturedEntities);
    RecordNestedNodeIds(scene, prefab, capturedEntities);
    return prefab;
}

} // namespace kb::scene
