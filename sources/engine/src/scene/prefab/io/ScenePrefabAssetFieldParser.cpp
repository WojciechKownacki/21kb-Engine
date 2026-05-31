#include "scene/prefab/io/ScenePrefabAssetFieldParser.hpp"

#include "scene/prefab/io/ScenePrefabAssetComponentParser.hpp"
#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"

#include <charconv>
#include <istream>
#include <optional>
#include <sstream>

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool ParseField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator != fields.end() && ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
}

[[nodiscard]] bool ParseQuat(const ScenePrefabAssetFieldMap& fields, std::string_view key, Quat& output) {
    const auto iterator = fields.find(std::string{ key });
    if (iterator == fields.end()) {
        return false;
    }

    std::istringstream stream{ iterator->second };
    std::string extra;
    return static_cast<bool>(stream >> output.x >> output.y >> output.z >> output.w) && !(stream >> extra);
}

[[nodiscard]] bool ParseEscapedString(const ScenePrefabAssetFieldMap& fields, std::string_view key, std::string& output) {
    const auto iterator = fields.find(std::string{ key });
    if (iterator == fields.end()) {
        return false;
    }

    std::optional<std::string> unescaped = ScenePrefabAssetEscaper::Unescape(iterator->second);
    if (!unescaped.has_value()) {
        return false;
    }

    output = std::move(*unescaped);
    return true;
}

} // namespace

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
    int parent = 0;
    bool visible = false;
    if (!ParseEscapedString(fields, ScenePrefabAssetFormat::NameKey, node.name)
        || !ParseField(fields, ScenePrefabAssetFormat::ParentKey, parent)
        || !ParseVec3(fields, ScenePrefabAssetFormat::LocalPositionKey, node.transform.localPosition)
        || !ParseQuat(fields, ScenePrefabAssetFormat::LocalRotationKey, node.transform.localRotation)
        || !ParseVec3(fields, ScenePrefabAssetFormat::LocalScaleKey, node.transform.localScale)
        || !ParseBool(fields, ScenePrefabAssetFormat::VisibleKey, visible)
        || !ScenePrefabAssetComponentParser::Parse(fields, node.components)) {
        return false;
    }

    node.parentNode = parent < 0 ? ScenePrefabNodeDesc::NoParent : static_cast<std::uint32_t>(parent);
    node.visibility.visible = visible;
    return true;
}

template <typename T>
bool ScenePrefabAssetFieldParser::ParseNumber(std::string_view text, T& output) {
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, output);
    return result.ec == std::errc{} && result.ptr == last;
}

template bool ScenePrefabAssetFieldParser::ParseNumber<std::size_t>(std::string_view, std::size_t&);
template bool ScenePrefabAssetFieldParser::ParseNumber<int>(std::string_view, int&);
template bool ScenePrefabAssetFieldParser::ParseNumber<std::uint64_t>(std::string_view, std::uint64_t&);
template bool ScenePrefabAssetFieldParser::ParseNumber<float>(std::string_view, float&);

} // namespace kb::scene
