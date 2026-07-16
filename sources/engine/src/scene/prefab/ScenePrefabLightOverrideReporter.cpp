#include "scene/prefab/ScenePrefabLightOverrideReporter.hpp"

#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"
#include "scene/prefab/ScenePrefabOverrideValueFormatter.hpp"

#include <string>

namespace kb::scene {
namespace {

[[nodiscard]] bool Equal(const LightComponent& lhs, const LightComponent& rhs) noexcept {
    return lhs.kind == rhs.kind
        && ScenePrefabOverrideValueFormatter::Equal(lhs.color, rhs.color)
        && lhs.intensity == rhs.intensity
        && lhs.range == rhs.range
        && lhs.innerConeDegrees == rhs.innerConeDegrees
        && lhs.outerConeDegrees == rhs.outerConeDegrees
        && lhs.areaWidth == rhs.areaWidth
        && lhs.areaHeight == rhs.areaHeight
        && lhs.contactShadowLength == rhs.contactShadowLength
        && lhs.volumetricScattering == rhs.volumetricScattering
        && lhs.castsShadow == rhs.castsShadow
        && lhs.useColorTemperature == rhs.useColorTemperature
        && lhs.colorTemperatureKelvin == rhs.colorTemperatureKelvin
        && lhs.layerMask == rhs.layerMask;
}

} // namespace

void ScenePrefabLightOverrideReporter::Append(SceneComponents components, SceneEntity entity, const std::optional<LightComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const LightComponent* actual = components.Lights().TryGet(entity);
    if (actual == nullptr && !expected.has_value()) {
        return;
    }
    if (actual != nullptr && expected.has_value() && Equal(*actual, *expected)) {
        return;
    }
    if (actual == nullptr) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light", "null", ScenePrefabOverrideFlag::Light);
        return;
    }
    if (!expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light", "present", ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.kind", std::to_string(static_cast<int>(actual->kind)), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.color", ScenePrefabOverrideValueFormatter::ToString(actual->color), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.intensity", ScenePrefabOverrideValueFormatter::ToString(actual->intensity), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.range", ScenePrefabOverrideValueFormatter::ToString(actual->range), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.innerConeDegrees", ScenePrefabOverrideValueFormatter::ToString(actual->innerConeDegrees), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.outerConeDegrees", ScenePrefabOverrideValueFormatter::ToString(actual->outerConeDegrees), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.areaWidth", ScenePrefabOverrideValueFormatter::ToString(actual->areaWidth), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.areaHeight", ScenePrefabOverrideValueFormatter::ToString(actual->areaHeight), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.contactShadowLength", ScenePrefabOverrideValueFormatter::ToString(actual->contactShadowLength), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.volumetricScattering", ScenePrefabOverrideValueFormatter::ToString(actual->volumetricScattering), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.castsShadow", ScenePrefabOverrideValueFormatter::ToString(actual->castsShadow), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.useColorTemperature", ScenePrefabOverrideValueFormatter::ToString(actual->useColorTemperature), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.colorTemperatureKelvin", ScenePrefabOverrideValueFormatter::ToString(actual->colorTemperatureKelvin), ScenePrefabOverrideFlag::Light);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.layerMask", std::to_string(actual->layerMask), ScenePrefabOverrideFlag::Light);
        return;
    }
    if (actual->kind != expected->kind) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.kind", std::to_string(static_cast<int>(actual->kind)), ScenePrefabOverrideFlag::Light);
    }
    if (!ScenePrefabOverrideValueFormatter::Equal(actual->color, expected->color)) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.color", ScenePrefabOverrideValueFormatter::ToString(actual->color), ScenePrefabOverrideFlag::Light);
    }
    if (actual->intensity != expected->intensity) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.intensity", ScenePrefabOverrideValueFormatter::ToString(actual->intensity), ScenePrefabOverrideFlag::Light);
    }
    if (actual->range != expected->range) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.range", ScenePrefabOverrideValueFormatter::ToString(actual->range), ScenePrefabOverrideFlag::Light);
    }
    if (actual->innerConeDegrees != expected->innerConeDegrees) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.innerConeDegrees", ScenePrefabOverrideValueFormatter::ToString(actual->innerConeDegrees), ScenePrefabOverrideFlag::Light);
    }
    if (actual->outerConeDegrees != expected->outerConeDegrees) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.outerConeDegrees", ScenePrefabOverrideValueFormatter::ToString(actual->outerConeDegrees), ScenePrefabOverrideFlag::Light);
    }
    if (actual->areaWidth != expected->areaWidth) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.areaWidth", ScenePrefabOverrideValueFormatter::ToString(actual->areaWidth), ScenePrefabOverrideFlag::Light);
    }
    if (actual->areaHeight != expected->areaHeight) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.areaHeight", ScenePrefabOverrideValueFormatter::ToString(actual->areaHeight), ScenePrefabOverrideFlag::Light);
    }
    if (actual->contactShadowLength != expected->contactShadowLength) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.contactShadowLength", ScenePrefabOverrideValueFormatter::ToString(actual->contactShadowLength), ScenePrefabOverrideFlag::Light);
    }
    if (actual->volumetricScattering != expected->volumetricScattering) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.volumetricScattering", ScenePrefabOverrideValueFormatter::ToString(actual->volumetricScattering), ScenePrefabOverrideFlag::Light);
    }
    if (actual->castsShadow != expected->castsShadow) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.castsShadow", actual->castsShadow ? "true" : "false", ScenePrefabOverrideFlag::Light);
    }
    if (actual->useColorTemperature != expected->useColorTemperature) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.useColorTemperature", actual->useColorTemperature ? "true" : "false", ScenePrefabOverrideFlag::Light);
    }
    if (actual->colorTemperatureKelvin != expected->colorTemperatureKelvin) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.colorTemperatureKelvin", ScenePrefabOverrideValueFormatter::ToString(actual->colorTemperatureKelvin), ScenePrefabOverrideFlag::Light);
    }
    if (actual->layerMask != expected->layerMask) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light.layerMask", std::to_string(actual->layerMask), ScenePrefabOverrideFlag::Light);
    }
}

} // namespace kb::scene
