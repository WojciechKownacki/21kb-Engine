#include "scene/prefab/io/ScenePrefabAssetLightParser.hpp"

#include "engine/scene/SceneComponents.hpp"

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool ParseField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator != fields.end() && ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
}

template <typename T>
[[nodiscard]] bool ParseOptionalField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator == fields.end() || ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
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
        || kind > static_cast<int>(LightKind::Tube)
        || !ScenePrefabAssetFieldParser::ParseVec3(fields, "light.color", light.color)
        || !ParseField(fields, "light.intensity", light.intensity)
        || !ParseField(fields, "light.range", light.range)
        || !ParseField(fields, "light.innerConeDegrees", light.innerConeDegrees)
        || !ParseField(fields, "light.outerConeDegrees", light.outerConeDegrees)
        || !ParseOptionalField(fields, "light.areaWidth", light.areaWidth)
        || !ParseOptionalField(fields, "light.areaHeight", light.areaHeight)
        || !ParseOptionalField(fields, "light.contactShadowLength", light.contactShadowLength)
        || !ParseOptionalField(fields, "light.volumetricScattering", light.volumetricScattering)
        || !ParseOptionalBool(fields, "light.castsShadow", light.castsShadow)
        || !ParseOptionalBool(fields, "light.useColorTemperature", light.useColorTemperature)
        || !ParseOptionalField(fields, "light.colorTemperatureKelvin", light.colorTemperatureKelvin)
        || !ParseOptionalField(fields, "light.layerMask", light.layerMask)) {
        return false;
    }

    light.kind = static_cast<LightKind>(kind);
    components.light = light;
    return true;
}

} // namespace kb::scene
