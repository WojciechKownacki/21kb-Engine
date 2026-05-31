#include "scene/prefab/io/ScenePrefabAssetReader.hpp"

#include "scene/prefab/ScenePrefabValidator.hpp"
#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"

#include <fstream>

namespace kb::scene {

bool ScenePrefabAssetReader::Read(const std::filesystem::path& path, ScenePrefabAssetReadResult& output) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return false;
    }

    ScenePrefabAssetReadResult result;
    std::string line;
    if (!ScenePrefabAssetFieldParser::ReadLine(input, line) || line != ScenePrefabAssetFormat::Header) {
        return false;
    }

    std::string key;
    std::string value;
    if (!ScenePrefabAssetFieldParser::ReadLine(input, line) || !ScenePrefabAssetFieldParser::SplitKeyValue(line, key, value) || key != ScenePrefabAssetFormat::NameKey) {
        return false;
    }
    std::optional<std::string> name = ScenePrefabAssetEscaper::Unescape(value);
    if (!name.has_value()) {
        return false;
    }
    result.name = std::move(*name);

    std::size_t nodeCount = 0;
    if (!ScenePrefabAssetFieldParser::ReadLine(input, line)
        || !ScenePrefabAssetFieldParser::SplitKeyValue(line, key, value)
        || key != ScenePrefabAssetFormat::NodesKey
        || !ScenePrefabAssetFieldParser::ParseNumber(value, nodeCount)) {
        return false;
    }
    result.prefab.Reserve(nodeCount);

    ScenePrefabAssetFieldMap fields;
    for (std::size_t index = 0; index < nodeCount; ++index) {
        if (!ScenePrefabAssetFieldParser::ReadLine(input, line) || line != ScenePrefabAssetFormat::NodeMarker || !ScenePrefabAssetFieldParser::ReadNodeFields(input, fields)) {
            return false;
        }

        ScenePrefabNodeDesc node;
        if (!ScenePrefabAssetFieldParser::ParseNode(fields, node)) {
            return false;
        }
        static_cast<void>(result.prefab.AddNode(std::move(node)));
    }

    if (ScenePrefabAssetFieldParser::ReadLine(input, line)) {
        return false;
    }
    if (!ScenePrefabValidator::IsValid(result.prefab)) {
        return false;
    }

    output = std::move(result);
    return true;
}

} // namespace kb::scene
