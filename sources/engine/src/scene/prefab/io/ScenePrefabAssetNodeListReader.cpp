#include "scene/prefab/io/ScenePrefabAssetNodeListReader.hpp"

#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetNodeParser.hpp"

#include <istream>
#include <string>
#include <utility>

namespace kb::scene {

namespace {

[[nodiscard]] bool HasNestedOverrideNodeId(const ScenePrefabAssetFieldMap& fields, std::size_t index) {
    const std::string key = std::string{ ScenePrefabAssetFormat::NestedOverridePrefix } + std::to_string(index) + "." + std::string{ ScenePrefabAssetFormat::OverrideNodeIdKey };
    return fields.contains(key);
}

void DetectMigrationNeeds(const ScenePrefabAssetFieldMap& fields, bool* missingNodeStableIds, bool* missingOverrideNodeIds) {
    if (missingNodeStableIds != nullptr && !fields.contains(std::string{ ScenePrefabAssetFormat::NodeStableIdKey })) {
        *missingNodeStableIds = true;
    }
    if (missingOverrideNodeIds == nullptr) {
        return;
    }

    const auto count = fields.find(std::string{ ScenePrefabAssetFormat::NestedOverrideCountKey });
    if (count == fields.end()) {
        return;
    }

    std::size_t overrideCount = 0;
    if (!ScenePrefabAssetFieldParser::ParseNumber(count->second, overrideCount)) {
        return;
    }
    for (std::size_t index = 0; index < overrideCount; ++index) {
        if (!HasNestedOverrideNodeId(fields, index)) {
            *missingOverrideNodeIds = true;
            return;
        }
    }
}

} // namespace

bool ScenePrefabAssetNodeListReader::Read(std::istream& input, std::size_t nodeCount, ScenePrefab& prefab, bool* missingNodeStableIds, bool* missingOverrideNodeIds) {
    prefab.Reserve(nodeCount);
    ScenePrefabAssetFieldMap fields;
    std::string line;
    for (std::size_t index = 0; index < nodeCount; ++index) {
        if (!ScenePrefabAssetFieldParser::ReadLine(input, line) || line != ScenePrefabAssetFormat::NodeMarker || !ScenePrefabAssetFieldParser::ReadNodeFields(input, fields)) {
            return false;
        }
        DetectMigrationNeeds(fields, missingNodeStableIds, missingOverrideNodeIds);

        ScenePrefabNodeDesc node;
        if (!ScenePrefabAssetNodeParser::Parse(fields, node)) {
            return false;
        }
        static_cast<void>(prefab.AddNode(std::move(node)));
    }
    return true;
}

} // namespace kb::scene
