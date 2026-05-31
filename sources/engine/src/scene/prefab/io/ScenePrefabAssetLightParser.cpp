#include "scene/prefab/io/ScenePrefabAssetLightParser.hpp"

#include "engine/scene/SceneComponents.hpp"

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool ParseField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator != fields.end() && ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
}

} // namespace

bool ScenePrefabAssetLightParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasLight = false;
    if (!ScenePrefabAssetFieldParser::ParseBool(fields, "light", hasLight)) {
        return false;
    }
    if (!hasLight) {
        return true;
    }

    int kind = 0;
    LightComponent light;
    if (!ParseField(fields, "light.kind", kind)
        || kind < static_cast<int>(LightKind::Directional)
        || kind > static_cast<int>(LightKind::Spot)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "light.color", light.color)
        || !ParseField(fields, "light.intensity", light.intensity)
        || !ParseField(fields, "light.range", light.range)
        || !ParseField(fields, "light.innerConeDegrees", light.innerConeDegrees)
        || !ParseField(fields, "light.outerConeDegrees", light.outerConeDegrees)) {
        return false;
    }

    light.kind = static_cast<LightKind>(kind);
    components.light = light;
    return true;
}

} // namespace kb::scene
