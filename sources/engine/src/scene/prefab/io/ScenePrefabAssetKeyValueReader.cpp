#include "scene/prefab/io/ScenePrefabAssetKeyValueReader.hpp"

#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"
#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"

#include <istream>
#include <optional>
#include <utility>

namespace kb::scene {

bool ScenePrefabAssetKeyValueReader::Read(std::istream& input, std::string_view expectedKey, std::string& value) {
    std::string line;
    std::string key;
    return ScenePrefabAssetFieldParser::ReadLine(input, line)
        && ScenePrefabAssetFieldParser::SplitKeyValue(line, key, value)
        && key == expectedKey;
}

bool ScenePrefabAssetKeyValueReader::ReadEscaped(std::string_view value, std::string& output) {
    std::optional<std::string> unescaped = ScenePrefabAssetEscaper::Unescape(value);
    if (!unescaped.has_value()) {
        return false;
    }

    output = std::move(*unescaped);
    return true;
}

} // namespace kb::scene
