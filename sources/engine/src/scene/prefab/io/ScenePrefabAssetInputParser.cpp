#include "scene/prefab/io/ScenePrefabAssetInputParser.hpp"

#include "engine/scene/SceneComponents.hpp"

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool ParseField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator != fields.end() && ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
}

[[nodiscard]] bool ParseOptionalBool(const ScenePrefabAssetFieldMap& fields, std::string_view key, bool& output) {
    const auto iterator = fields.find(std::string{ key });
    if (iterator == fields.end()) {
        return true;
    }

    int value = 0;
    if (!ScenePrefabAssetFieldParser::ParseNumber(iterator->second, value) || (value != 0 && value != 1)) {
        return false;
    }
    output = value != 0;
    return true;
}

} // namespace

bool ScenePrefabAssetInputParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasInput = false;
    if (!ScenePrefabAssetFieldParser::ParseBool(fields, "input", hasInput)) {
        return false;
    }
    if (!hasInput) {
        return true;
    }

    InputComponent input;
    int priority = 0;
    if (!ParseField(fields, "input.mappingContextAssetId", input.mappingContextAssetId)
        || !ParseField(fields, "input.priority", priority)
        || !ParseOptionalBool(fields, "input.enabled", input.enabled)) {
        return false;
    }

    input.priority = priority;
    components.input = input;
    return true;
}

} // namespace kb::scene
