#include "scene/prefab/ScenePrefabCaptureService.hpp"

#include "scene/prefab/ScenePrefabCaptureValidator.hpp"
#include "scene/prefab/ScenePrefabCaptureTraversal.hpp"
#include "scene/prefab/ScenePrefabHierarchyCounter.hpp"

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
        // UI navigation links hold live entity ids, which a reload, an undo snapshot or a packaged
        // scene all hand out afresh. Persist them as the target's stable node id instead. A target
        // outside what is being captured cannot be expressed by this prefab, so that link is dropped
        // rather than kept as an id that would later name an unrelated object.
        // An option's content widget is a child of the dropdown, so it is always inside the capture.
        if (node->components.ui.dropdown.has_value()) {
            UIDropdown& dropdown = *node->components.ui.dropdown;
            for (std::uint32_t option = 0U; option < dropdown.optionCount; ++option) {
                std::uint64_t& content = dropdown.options[option].content;
                if (content == 0U) continue;
                const auto target = stableNodeIds.find(content);
                content = target == stableNodeIds.end() ? 0U : target->second;
            }
        }
        if (node->components.ui.selectable.has_value()) {
            UISelectable& selectable = *node->components.ui.selectable;
            for (std::uint64_t* link : { &selectable.navigationUp, &selectable.navigationDown,
                     &selectable.navigationLeft, &selectable.navigationRight }) {
                if (*link == 0U) continue;
                const auto target = stableNodeIds.find(*link);
                *link = target == stableNodeIds.end() ? 0U : target->second;
            }
        }
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
    return prefab;
}

} // namespace kb::scene
