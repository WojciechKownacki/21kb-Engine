#include "scene/prefab/io/ScenePrefabAssetNodeFieldParser.hpp"

#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"

#include <optional>
#include <sstream>
#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] const std::string* FindField(const ScenePrefabAssetFieldMap& fields, std::string_view key) {
    const auto iterator = fields.find(std::string{ key });
    return iterator == fields.end() ? nullptr : &iterator->second;
}

} // namespace

bool ScenePrefabAssetNodeFieldParser::ParseInt(const ScenePrefabAssetFieldMap& fields, std::string_view key, int& output) {
    const std::string* value = FindField(fields, key);
    return value != nullptr && ScenePrefabAssetFieldParser::ParseNumber(*value, output);
}

bool ScenePrefabAssetNodeFieldParser::ParseQuat(const ScenePrefabAssetFieldMap& fields, std::string_view key, Quat& output) {
    const std::string* value = FindField(fields, key);
    if (value == nullptr) {
        return false;
    }

    std::istringstream stream{ *value };
    std::string extra;
    return static_cast<bool>(stream >> output.x >> output.y >> output.z >> output.w) && !(stream >> extra);
}

bool ScenePrefabAssetNodeFieldParser::ParseEscapedString(const ScenePrefabAssetFieldMap& fields, std::string_view key, std::string& output) {
    const std::string* value = FindField(fields, key);
    if (value == nullptr) {
        return false;
    }

    std::optional<std::string> unescaped = ScenePrefabAssetEscaper::Unescape(*value);
    if (!unescaped.has_value()) {
        return false;
    }

    output = std::move(*unescaped);
    return true;
}

bool ScenePrefabAssetNodeFieldParser::ParseOptionalEscapedString(const ScenePrefabAssetFieldMap& fields, std::string_view key, std::string& output) {
    const std::string* value = FindField(fields, key);
    if (value == nullptr) {
        output.clear();
        return true;
    }

    std::optional<std::string> unescaped = ScenePrefabAssetEscaper::Unescape(*value);
    if (!unescaped.has_value()) {
        return false;
    }

    output = std::move(*unescaped);
    return true;
}

} // namespace kb::scene
