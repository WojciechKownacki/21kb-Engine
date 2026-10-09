#include "scene/prefab/io/ScenePrefabAssetFieldWriter.hpp"

#include "scene/prefab/io/ScenePrefabAssetComponentWriter.hpp"
#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/transform/SceneTransformResiduals.hpp"

#include <array>
#include <charconv>
#include <ostream>
#include <string>
#include <string_view>

namespace kb::scene {
namespace {

void WriteVec3(std::ostream& output, std::string_view key, Vec3 value) {
    output << key << '=' << value.x << ' ' << value.y << ' ' << value.z << '\n';
}

// The shortest text of each float, which reads back as exactly the same value.
void WriteExactVec3(std::ostream& output, std::string_view key, Vec3 value) {
    const std::array<float, 3U> components{ value.x, value.y, value.z };
    output << key << '=';
    for (std::size_t index = 0U; index < components.size(); ++index) {
        std::array<char, 32> buffer{};
        const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), components[index]);
        if (index != 0U) output << ' ';
        output.write(buffer.data(), result.ptr - buffer.data());
    }
    output << '\n';
}

void WriteQuat(std::ostream& output, std::string_view key, Quat value) {
    output << key << '=' << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w << '\n';
}

void WriteNestedOverride(std::ostream& output, std::size_t index, const ScenePrefabPropertyOverride& property) {
    const std::string prefix = std::string{ ScenePrefabAssetFormat::NestedOverridePrefix } + std::to_string(index) + ".";
    output << prefix << ScenePrefabAssetFormat::OverrideNodeKey << '=' << property.nodeIndex << '\n';
    if (property.nodeId != ScenePrefabNodeDesc::InvalidStableId) {
        output << prefix << ScenePrefabAssetFormat::OverrideNodeIdKey << '=' << property.nodeId << '\n';
    }
    output << prefix << ScenePrefabAssetFormat::OverridePropertyPathKey << '=' << ScenePrefabAssetEscaper::Escape(property.propertyPath) << '\n';
    output << prefix << ScenePrefabAssetFormat::OverrideValueKey << '=' << ScenePrefabAssetEscaper::Escape(property.value) << '\n';
    output << prefix << ScenePrefabAssetFormat::OverrideFlagKey << '=' << static_cast<std::uint32_t>(property.flag) << '\n';
}

} // namespace

void ScenePrefabAssetFieldWriter::WriteNode(std::ostream& output, const ScenePrefabNodeDesc& node) {
    output << ScenePrefabAssetFormat::NodeMarker << '\n';
    output << ScenePrefabAssetFormat::NodeStableIdKey << '=' << node.stableId << '\n';
    output << ScenePrefabAssetFormat::NameKey << '=' << ScenePrefabAssetEscaper::Escape(node.name) << '\n';
    output << ScenePrefabAssetFormat::NestedPrefabGuidKey << '=' << ScenePrefabAssetEscaper::Escape(node.nestedPrefabGuid) << '\n';
    output << ScenePrefabAssetFormat::NestedOverrideCountKey << '=' << node.nestedPrefabOverrides.size() << '\n';
    for (std::size_t index = 0; index < node.nestedPrefabOverrides.size(); ++index) {
        WriteNestedOverride(output, index, node.nestedPrefabOverrides[index]);
    }
    output << ScenePrefabAssetFormat::ParentKey << '=' << (node.parentNode == ScenePrefabNodeDesc::NoParent ? -1 : static_cast<int>(node.parentNode)) << '\n';
    // The translation round-trips exactly: its float view, and the part below float precision when it has one.
    WriteExactVec3(output, ScenePrefabAssetFormat::LocalPositionKey, node.transform.localPosition);
    if (const Vec3 residual = FittingResidual(node.transform.localPosition, node.localPositionResidual); !IsZero(residual)) {
        WriteExactVec3(output, ScenePrefabAssetFormat::LocalPositionResidualKey, residual);
    }
    WriteQuat(output, ScenePrefabAssetFormat::LocalRotationKey, node.transform.localRotation);
    WriteVec3(output, ScenePrefabAssetFormat::LocalScaleKey, node.transform.localScale);
    const VisibilityMode visibilityMode = node.visibility.visible
        ? node.visibility.mode : VisibilityMode::Hidden;
    output << ScenePrefabAssetFormat::VisibleKey << '=' << (visibilityMode != VisibilityMode::Hidden ? 1 : 0) << '\n';
    output << ScenePrefabAssetFormat::VisibilityModeKey << '=' << static_cast<unsigned int>(visibilityMode) << '\n';
    output << ScenePrefabAssetFormat::VisibilityMaskKey << '=' << node.visibility.mask << '\n';
    ScenePrefabAssetComponentWriter::Write(output, node.components);
    output << ScenePrefabAssetFormat::EndNodeMarker << '\n';
}

} // namespace kb::scene
