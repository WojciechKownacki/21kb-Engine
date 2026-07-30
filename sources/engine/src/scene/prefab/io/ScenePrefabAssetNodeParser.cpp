#include "scene/prefab/io/ScenePrefabAssetNodeParser.hpp"

#include "scene/prefab/io/ScenePrefabAssetComponentParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetNodeFieldParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetNestedOverrideParser.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

bool ScenePrefabAssetNodeParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeDesc& node) {
    int parent = 0;
    bool visible = false;
    std::uint64_t stableId = ScenePrefabNodeDesc::InvalidStableId;
    const auto stableIdField = fields.find(std::string{ ScenePrefabAssetFormat::NodeStableIdKey });
    if (stableIdField != fields.end() && !ScenePrefabAssetFieldParser::ParseNumber(stableIdField->second, stableId)) {
        return false;
    }
    if (!ScenePrefabAssetNodeFieldParser::ParseEscapedString(fields, ScenePrefabAssetFormat::NameKey, node.name)
        || !ScenePrefabAssetNodeFieldParser::ParseOptionalEscapedString(fields, ScenePrefabAssetFormat::NestedPrefabGuidKey, node.nestedPrefabGuid)
        || !ScenePrefabAssetNestedOverrideParser::Parse(fields, node)
        || !ScenePrefabAssetNodeFieldParser::ParseInt(fields, ScenePrefabAssetFormat::ParentKey, parent)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, ScenePrefabAssetFormat::LocalPositionKey, node.transform.localPosition)
        || !ScenePrefabAssetNodeFieldParser::ParseQuat(fields, ScenePrefabAssetFormat::LocalRotationKey, node.transform.localRotation)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, ScenePrefabAssetFormat::LocalScaleKey, node.transform.localScale)
        || !ScenePrefabAssetFieldParser::ParseBool(fields, ScenePrefabAssetFormat::VisibleKey, visible)
        || !ScenePrefabAssetComponentParser::Parse(fields, node.components)) {
        return false;
    }

    node.stableId = stableId;
    node.parentNode = parent < 0 ? ScenePrefabNodeDesc::NoParent : static_cast<std::uint32_t>(parent);
    node.visibility.mode = visible ? VisibilityMode::Visible : VisibilityMode::Hidden;
    if (const auto mode = fields.find(std::string{ ScenePrefabAssetFormat::VisibilityModeKey }); mode != fields.end()) {
        std::uint32_t rawMode = 0U;
        if (!ScenePrefabAssetFieldParser::ParseNumber(mode->second, rawMode)) return false;
        node.visibility.mode = static_cast<VisibilityMode>(rawMode);
        if (!IsVisibilityModeValid(node.visibility.mode)) return false;
    }
    if (const auto mask = fields.find(std::string{ ScenePrefabAssetFormat::VisibilityMaskKey }); mask != fields.end() &&
        !ScenePrefabAssetFieldParser::ParseNumber(mask->second, node.visibility.mask)) {
        return false;
    }
    node.visibility.visible = node.visibility.mode != VisibilityMode::Hidden;
    return true;
}

} // namespace kb::scene
