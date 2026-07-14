#include "scene/prefab/ScenePrefabVariantMaterializer.hpp"

#include "scene/prefab/ScenePrefabPropertyOverrideApplier.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <utility>

namespace kb::scene {

bool ScenePrefabVariantMaterializer::Materialize(const ScenePrefab& basePrefab, std::span<const ScenePrefabPropertyOverride> overrides, ScenePrefab& output) {
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
    if (!ScenePrefabValidator::IsValid(variant) || variant.Empty()) {
        return false;
    }

    output = std::move(variant);
    return true;
}

} // namespace kb::scene
