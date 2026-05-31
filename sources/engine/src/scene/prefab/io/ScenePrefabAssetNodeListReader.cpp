#include "scene/prefab/io/ScenePrefabAssetNodeListReader.hpp"

#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetNodeParser.hpp"

#include <istream>
#include <string>
#include <utility>

namespace kb::scene {

bool ScenePrefabAssetNodeListReader::Read(std::istream& input, std::size_t nodeCount, ScenePrefab& prefab) {
    prefab.Reserve(nodeCount);
    ScenePrefabAssetFieldMap fields;
    std::string line;
    for (std::size_t index = 0; index < nodeCount; ++index) {
        if (!ScenePrefabAssetFieldParser::ReadLine(input, line) || line != ScenePrefabAssetFormat::NodeMarker || !ScenePrefabAssetFieldParser::ReadNodeFields(input, fields)) {
            return false;
        }

        ScenePrefabNodeDesc node;
        if (!ScenePrefabAssetNodeParser::Parse(fields, node)) {
            return false;
        }
        static_cast<void>(prefab.AddNode(std::move(node)));
    }
    return true;
}

} // namespace kb::scene
