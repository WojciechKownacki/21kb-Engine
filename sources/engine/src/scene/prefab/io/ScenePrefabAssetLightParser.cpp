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
        || !ParseOptionalField(fields, "light.layerMask", light.layerMask)
        || !ParseOptionalField(fields, "light.sourceRadius", light.sourceRadius)
        || !ParseOptionalField(fields, "light.shadowStrength", light.shadowStrength)
        || !ParseOptionalField(fields, "light.shadowDistance", light.shadowDistance)
        || !ParseOptionalField(fields, "light.shadowBias", light.shadowBias)
        || !ParseOptionalField(fields, "light.shadowNormalBias", light.shadowNormalBias)
        || !ParseOptionalField(fields, "light.shadowCascadeCount", light.shadowCascadeCount)
        || !ParseOptionalField(fields, "light.shadowCascadeDistribution", light.shadowCascadeDistribution)) {
        return false;
    }
    std::uint32_t shadowFilter = static_cast<std::uint32_t>(light.shadowFilter);
    std::uint32_t shadowQuality = static_cast<std::uint32_t>(light.shadowQuality);
    if (!ParseOptionalField(fields, "light.shadowFilter", shadowFilter) || shadowFilter > static_cast<std::uint32_t>(LightShadowFilter::Pcss) ||
        !ParseOptionalField(fields, "light.shadowQuality", shadowQuality) || shadowQuality > static_cast<std::uint32_t>(LightShadowQuality::Ultra)) {
        return false;
    }
    light.shadowFilter = static_cast<LightShadowFilter>(shadowFilter);
    light.shadowQuality = static_cast<LightShadowQuality>(shadowQuality);
    std::uint32_t mobility = static_cast<std::uint32_t>(light.mobility);
    if (!ParseOptionalField(fields, "light.mobility", mobility) ||
        mobility > static_cast<std::uint32_t>(LightMobility::Static)) {
        return false;
    }
    light.mobility = static_cast<LightMobility>(mobility);

    light.kind = static_cast<LightKind>(kind);
    if (!IsLightComponentValid(light)) {
        return false;
    }
    components.light = light;
    return true;
}

} // namespace kb::scene
