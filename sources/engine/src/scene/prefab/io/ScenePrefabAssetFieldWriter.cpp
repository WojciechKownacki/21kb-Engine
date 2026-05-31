#include "scene/prefab/io/ScenePrefabAssetFieldWriter.hpp"

#include "scene/prefab/io/ScenePrefabAssetComponentWriter.hpp"
#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"

#include <ostream>
#include <string_view>

namespace kb::scene {
namespace {

void WriteVec3(std::ostream& output, std::string_view key, Vec3 value) {
    output << key << '=' << value.x << ' ' << value.y << ' ' << value.z << '\n';
}

void WriteQuat(std::ostream& output, std::string_view key, Quat value) {
    output << key << '=' << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w << '\n';
}

} // namespace

void ScenePrefabAssetFieldWriter::WriteNode(std::ostream& output, const ScenePrefabNodeDesc& node) {
    output << ScenePrefabAssetFormat::NodeMarker << '\n';
    output << ScenePrefabAssetFormat::NameKey << '=' << ScenePrefabAssetEscaper::Escape(node.name) << '\n';
    output << ScenePrefabAssetFormat::ParentKey << '=' << (node.parentNode == ScenePrefabNodeDesc::NoParent ? -1 : static_cast<int>(node.parentNode)) << '\n';
    WriteVec3(output, ScenePrefabAssetFormat::LocalPositionKey, node.transform.localPosition);
    WriteQuat(output, ScenePrefabAssetFormat::LocalRotationKey, node.transform.localRotation);
    WriteVec3(output, ScenePrefabAssetFormat::LocalScaleKey, node.transform.localScale);
    output << ScenePrefabAssetFormat::VisibleKey << '=' << (node.visibility.visible ? 1 : 0) << '\n';
    ScenePrefabAssetComponentWriter::Write(output, node.components);
    output << ScenePrefabAssetFormat::EndNodeMarker << '\n';
}

} // namespace kb::scene
