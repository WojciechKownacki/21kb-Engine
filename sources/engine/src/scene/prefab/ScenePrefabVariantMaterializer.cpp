#include "scene/prefab/ScenePrefabVariantMaterializer.hpp"

#include "scene/prefab/ScenePrefabPropertyOverrideApplier.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <utility>

namespace kb::scene {

bool ScenePrefabVariantMaterializer::Materialize(
    const ScenePrefab& basePrefab,
    std::span<const ScenePrefabPropertyOverride> overrides,
    std::span<const ScenePrefabVariantAddedSubtree> addedChildren,
    ScenePrefab& output) {
    ScenePrefab variant = basePrefab;
    for (const ScenePrefabPropertyOverride& property : overrides) {
        ScenePrefabNodeDesc* node = property.nodeId != ScenePrefabNodeDesc::InvalidStableId
            ? variant.TryGetMutableNodeByStableId(property.nodeId)
            : variant.TryGetMutableNode(property.nodeIndex);
        if (node == nullptr) {
            return false;
        }
        if (property.propertyPath == "parent") {
            // LIB-092: a "parent" override changes NODE TOPOLOGY (which
            // other node this one nests under), not a leaf value —
            // ScenePrefabPropertyOverrideApplier::Apply only ever sees one
            // node at a time and has no case for it (by design: it's a
            // per-node value applier, not a topology editor), so this is
            // resolved here instead, against `variant`'s own node list,
            // the same stable-node-id mechanism the live-scene apply path
            // (ScenePrefabInstanceSynchronizer) uses. An unresolved
            // objectReferenceNodeId (0 — the new parent lies outside this
            // prefab's own structure, or the override predates this field)
            // is honestly skipped rather than failing the whole variant,
            // matching the "children"/AddedChild report-only boundary.
            if (property.objectReferenceNodeId != ScenePrefabNodeDesc::InvalidStableId) {
                const std::uint32_t parentIndex = variant.FindNodeIndexByStableId(property.objectReferenceNodeId);
                if (parentIndex == ScenePrefabNodeDesc::NoParent) {
                    return false;
                }
                node->parentNode = parentIndex;
            }
            continue;
        }
        if (!ScenePrefabPropertyOverrideApplier::Apply(*node, property)) {
            return false;
        }
    }

    // LIB-092: append each variant added-child SUBTREE as real nodes under
    // its host base node, so the materialized variant prefab structurally
    // CONTAINS the added children — every downstream consumer (instantiation,
    // hashing, revert, save) then treats them as ordinary nodes with no
    // special-casing. Local parentNode indices in the captured subtree are
    // remapped onto the appended range; the subtree root (NoParent locally)
    // is re-parented onto the host node. A host node that no longer exists in
    // this variant drops its subtree honestly rather than corrupting the
    // node list — the same "reference outside the structure is unresolvable"
    // boundary the "parent" override already uses above.
    for (const ScenePrefabVariantAddedSubtree& added : addedChildren) {
        const std::uint32_t hostIndex = variant.FindNodeIndexByStableId(added.hostNodeId);
        if (hostIndex == ScenePrefabNodeDesc::NoParent) {
            continue;
        }
        const std::span<const ScenePrefabNodeDesc> subtreeNodes = added.subtree.Nodes();
        const std::uint32_t appendBase = static_cast<std::uint32_t>(variant.NodeCount());
        for (const ScenePrefabNodeDesc& subtreeNode : subtreeNodes) {
            ScenePrefabNodeDesc appended = subtreeNode;
            appended.parentNode = subtreeNode.parentNode == ScenePrefabNodeDesc::NoParent
                ? hostIndex
                : appendBase + subtreeNode.parentNode;
            // The captured subtree numbers its own stable ids from 1, which
            // collide with the base/variant node ids (also from 1) and would
            // fail validation as duplicates. Clearing the id lets AddNode
            // assign a fresh, unique one (NextStableNodeId over the growing
            // variant) — the exact mechanism the TEMPLATE applier relies on
            // for its added nodes, so added children number identically here.
            appended.stableId = ScenePrefabNodeDesc::InvalidStableId;
            static_cast<void>(variant.AddNode(std::move(appended)));
        }
    }

    if (!ScenePrefabValidator::IsValid(variant) || variant.Empty()) {
        return false;
    }

    output = std::move(variant);
    return true;
}

} // namespace kb::scene
