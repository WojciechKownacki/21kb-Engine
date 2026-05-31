#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"

#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"
#include "scene/prefab/io/ScenePrefabAssetNodeParser.hpp"

#include <charconv>
#include <cstdint>
#include <istream>
#include <sstream>
#include <string>
#include <type_traits>

namespace kb::scene {

bool ScenePrefabAssetFieldParser::ReadLine(std::istream& input, std::string& line) {
    if (!std::getline(input, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return true;
}

bool ScenePrefabAssetFieldParser::SplitKeyValue(std::string_view line, std::string& key, std::string& value) {
    const std::size_t separator = line.find('=');
    if (separator == std::string_view::npos) {
        return false;
    }
    key.assign(line.substr(0, separator));
    value.assign(line.substr(separator + 1));
    return !key.empty();
}

bool ScenePrefabAssetFieldParser::ReadNodeFields(std::istream& input, ScenePrefabAssetFieldMap& fields) {
    fields.clear();
    std::string line;
    while (ReadLine(input, line)) {
        if (line == ScenePrefabAssetFormat::EndNodeMarker) {
            return true;
        }

        std::string key;
        std::string value;
        if (!SplitKeyValue(line, key, value) || fields.contains(key)) {
            return false;
        }
        fields.emplace(std::move(key), std::move(value));
    }
    return false;
}

bool ScenePrefabAssetFieldParser::ParseBool(const ScenePrefabAssetFieldMap& fields, std::string_view key, bool& output) {
    const auto iterator = fields.find(std::string{ key });
    if (iterator == fields.end()) {
        return false;
    }

    int value = 0;
    if (!ParseNumber(iterator->second, value) || (value != 0 && value != 1)) {
        return false;
    }
    output = value != 0;
    return true;
}

bool ScenePrefabAssetFieldParser::ParseVec3(const ScenePrefabAssetFieldMap& fields, std::string_view key, Vec3& output) {
    const auto iterator = fields.find(std::string{ key });
    if (iterator == fields.end()) {
        return false;
    }

    std::istringstream stream{ iterator->second };
    std::string extra;
    return static_cast<bool>(stream >> output.x >> output.y >> output.z) && !(stream >> extra);
}

bool ScenePrefabAssetFieldParser::ParseNode(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeDesc& node) {
    return ScenePrefabAssetNodeParser::Parse(fields, node);
}

template <typename T>
bool ScenePrefabAssetFieldParser::ParseNumber(std::string_view text, T& output) {
    if constexpr (std::is_floating_point_v<T>) {
        std::istringstream stream{ std::string{ text } };
        std::string extra;
        return static_cast<bool>(stream >> output) && !(stream >> extra);
    } else {
        const char* first = text.data();
        const char* last = text.data() + text.size();
        const std::from_chars_result result = std::from_chars(first, last, output);
        return result.ec == std::errc{} && result.ptr == last;
    }
}

template bool ScenePrefabAssetFieldParser::ParseNumber<std::size_t>(std::string_view, std::size_t&);
template bool ScenePrefabAssetFieldParser::ParseNumber<int>(std::string_view, int&);
template bool ScenePrefabAssetFieldParser::ParseNumber<float>(std::string_view, float&);

} // namespace kb::scene
