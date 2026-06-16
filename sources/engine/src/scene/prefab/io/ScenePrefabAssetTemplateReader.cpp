#include "scene/prefab/io/ScenePrefabAssetTemplateReader.hpp"

#include "scene/prefab/ScenePrefabValidator.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetKeyValueReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetNodeListReader.hpp"

namespace kb::scene {
namespace {

[[nodiscard]] bool ReadNodeList(std::istream& input, ScenePrefabAssetReadResult& result) {
    std::string nodesValue;
    std::size_t nodeCount = 0;
    if (!ScenePrefabAssetKeyValueReader::Read(input, ScenePrefabAssetFormat::NodesKey, nodesValue) || !ScenePrefabAssetFieldParser::ParseNumber(nodesValue, nodeCount)) {
        return false;
    }

    return ScenePrefabAssetNodeListReader::Read(input, nodeCount, result.prefab, &result.missingNodeStableIds, &result.missingOverrideNodeIds) &&
        ScenePrefabValidator::IsValid(result.prefab);
}

} // namespace

bool ScenePrefabAssetTemplateReader::ReadLegacy(std::istream& input, ScenePrefabAssetReadResult& result) {
    result.kind = ScenePrefabAssetKind::Template;
    std::string value;
    if (!ScenePrefabAssetKeyValueReader::Read(input, ScenePrefabAssetFormat::NameKey, value) || !ScenePrefabAssetKeyValueReader::ReadEscaped(value, result.name)) {
        return false;
    }

    result.missingNodeStableIds = true;
    return ReadNodeList(input, result);
}

bool ScenePrefabAssetTemplateReader::ReadV2(std::istream& input, ScenePrefabAssetReadResult& result) {
    result.kind = ScenePrefabAssetKind::Template;
    return ReadNodeList(input, result);
}

} // namespace kb::scene
