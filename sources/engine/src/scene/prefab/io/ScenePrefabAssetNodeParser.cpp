#include "scene/prefab/io/ScenePrefabAssetNodeParser.hpp"

#include "scene/prefab/io/ScenePrefabAssetComponentParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetNodeFieldParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetNestedOverrideParser.hpp"

namespace kb::scene {

bool ScenePrefabAssetNodeParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeDesc& node) {
    int parent = 0;
    bool visible = false;
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

    node.parentNode = parent < 0 ? ScenePrefabNodeDesc::NoParent : static_cast<std::uint32_t>(parent);
    node.visibility.visible = visible;
    return true;
}

} // namespace kb::scene
