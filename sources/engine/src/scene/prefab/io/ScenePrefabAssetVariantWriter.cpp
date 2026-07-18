#include "scene/prefab/io/ScenePrefabAssetVariantWriter.hpp"

#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldWriter.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"

#include <cstdint>
#include <ostream>

namespace kb::scene {
namespace {

void WriteOverride(std::ostream& output, const ScenePrefabPropertyOverride& property) {
    output << ScenePrefabAssetFormat::OverrideMarker << '\n';
    output << ScenePrefabAssetFormat::OverrideNodeKey << '=' << property.nodeIndex << '\n';
    output << ScenePrefabAssetFormat::OverrideNodeIdKey << '=' << property.nodeId << '\n';
    output << ScenePrefabAssetFormat::OverridePropertyPathKey << '=' << ScenePrefabAssetEscaper::Escape(property.propertyPath) << '\n';
    output << ScenePrefabAssetFormat::OverrideValueKey << '=' << ScenePrefabAssetEscaper::Escape(property.value) << '\n';
    output << ScenePrefabAssetFormat::OverrideObjectReferenceKey << '=' << property.objectReference.Entity().Id() << '\n';
    output << ScenePrefabAssetFormat::OverrideObjectReferenceNodeIdKey << '=' << property.objectReferenceNodeId << '\n';
    output << ScenePrefabAssetFormat::OverrideFlagKey << '=' << static_cast<std::uint32_t>(property.flag) << '\n';
    output << ScenePrefabAssetFormat::EndOverrideMarker << '\n';
}

// LIB-092: mirror the template node-list encoding (nodes=<count> then a
// node/endnode block per node) for a variant added-child subtree, wrapped in an
// addedchild/endaddedchild block carrying the host node's stable id. Reusing
// ScenePrefabAssetFieldWriter::WriteNode keeps the subtree's on-disk shape
// byte-identical to any other node list, so future node fields serialize with
// no extra work here.
void WriteAddedChild(std::ostream& output, const ScenePrefabVariantAddedSubtree& added) {
    output << ScenePrefabAssetFormat::AddedChildMarker << '\n';
    output << ScenePrefabAssetFormat::AddedChildHostNodeIdKey << '=' << added.hostNodeId << '\n';
    output << ScenePrefabAssetFormat::NodesKey << '=' << added.subtree.NodeCount() << '\n';
    for (const ScenePrefabNodeDesc& node : added.subtree.Nodes()) {
        ScenePrefabAssetFieldWriter::WriteNode(output, node);
    }
    output << ScenePrefabAssetFormat::EndAddedChildMarker << '\n';
}

} // namespace

bool ScenePrefabAssetVariantWriter::CanWrite(const ScenePrefabAssetWriteDesc& asset) {
    return asset.kind == ScenePrefabAssetKind::Variant && !asset.baseGuid.empty() && asset.overrides != nullptr;
}

void ScenePrefabAssetVariantWriter::WriteBody(std::ostream& output, const ScenePrefabAssetWriteDesc& asset) {
    output << ScenePrefabAssetFormat::BaseGuidKey << '=' << ScenePrefabAssetEscaper::Escape(asset.baseGuid) << '\n';
    output << ScenePrefabAssetFormat::OverridesKey << '=' << asset.overrides->size() << '\n';
    for (const ScenePrefabPropertyOverride& property : *asset.overrides) {
        WriteOverride(output, property);
    }

    // LIB-092: the added-child section is only emitted when there is something
    // to write, so variants without added children keep the exact byte layout
    // older readers already accept (the count key stays absent, which the
    // reader treats as zero).
    if (asset.addedChildren != nullptr && !asset.addedChildren->empty()) {
        output << ScenePrefabAssetFormat::AddedChildrenKey << '=' << asset.addedChildren->size() << '\n';
        for (const ScenePrefabVariantAddedSubtree& added : *asset.addedChildren) {
            WriteAddedChild(output, added);
        }
    }
}

} // namespace kb::scene
