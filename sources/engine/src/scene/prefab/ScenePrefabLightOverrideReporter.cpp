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
        && lhs.outerConeDegrees == rhs.outerConeDegrees;
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
    if (actual == nullptr || !expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "light", actual == nullptr ? "null" : "present", ScenePrefabOverrideFlag::Light);
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
}

} // namespace kb::scene
