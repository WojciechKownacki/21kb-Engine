#include "scene/prefab/io/ScenePrefabAssetVariantWriter.hpp"

#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"

#include <cstdint>
#include <ostream>

namespace kb::scene {
namespace {

void WriteOverride(std::ostream& output, const ScenePrefabPropertyOverride& property) {
    output << ScenePrefabAssetFormat::OverrideMarker << '\n';
    output << ScenePrefabAssetFormat::OverrideNodeKey << '=' << property.nodeIndex << '\n';
    output << ScenePrefabAssetFormat::OverridePropertyPathKey << '=' << ScenePrefabAssetEscaper::Escape(property.propertyPath) << '\n';
    output << ScenePrefabAssetFormat::OverrideValueKey << '=' << ScenePrefabAssetEscaper::Escape(property.value) << '\n';
    output << ScenePrefabAssetFormat::OverrideObjectReferenceKey << '=' << property.objectReference.Entity().Id() << '\n';
    output << ScenePrefabAssetFormat::OverrideFlagKey << '=' << static_cast<std::uint32_t>(property.flag) << '\n';
    output << ScenePrefabAssetFormat::EndOverrideMarker << '\n';
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
}

} // namespace kb::scene
