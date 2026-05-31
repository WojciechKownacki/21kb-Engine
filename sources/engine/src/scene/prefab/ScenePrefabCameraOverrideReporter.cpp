#include "scene/prefab/ScenePrefabCameraOverrideReporter.hpp"

#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"
#include "scene/prefab/ScenePrefabOverrideValueFormatter.hpp"

#include <string>

namespace kb::scene {
namespace {

[[nodiscard]] bool Equal(const CameraComponent& lhs, const CameraComponent& rhs) noexcept {
    return lhs.projection == rhs.projection
        && lhs.verticalFovDegrees == rhs.verticalFovDegrees
        && lhs.orthographicHeight == rhs.orthographicHeight
        && lhs.nearClip == rhs.nearClip
        && lhs.farClip == rhs.farClip
        && lhs.primary == rhs.primary;
}

} // namespace

void ScenePrefabCameraOverrideReporter::Append(SceneComponents components, SceneEntity entity, const std::optional<CameraComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const CameraComponent* actual = components.Cameras().TryGet(entity);
    if (actual == nullptr && !expected.has_value()) {
        return;
    }
    if (actual != nullptr && expected.has_value() && Equal(*actual, *expected)) {
        return;
    }
    if (actual == nullptr || !expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera", actual == nullptr ? "null" : "present", ScenePrefabOverrideFlag::Camera);
        return;
    }
    if (actual->projection != expected->projection) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.projection", std::to_string(static_cast<int>(actual->projection)), ScenePrefabOverrideFlag::Camera);
    }
    if (actual->verticalFovDegrees != expected->verticalFovDegrees) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.verticalFovDegrees", ScenePrefabOverrideValueFormatter::ToString(actual->verticalFovDegrees), ScenePrefabOverrideFlag::Camera);
    }
    if (actual->orthographicHeight != expected->orthographicHeight) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.orthographicHeight", ScenePrefabOverrideValueFormatter::ToString(actual->orthographicHeight), ScenePrefabOverrideFlag::Camera);
    }
    if (actual->nearClip != expected->nearClip) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.nearClip", ScenePrefabOverrideValueFormatter::ToString(actual->nearClip), ScenePrefabOverrideFlag::Camera);
    }
    if (actual->farClip != expected->farClip) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.farClip", ScenePrefabOverrideValueFormatter::ToString(actual->farClip), ScenePrefabOverrideFlag::Camera);
    }
    if (actual->primary != expected->primary) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.primary", ScenePrefabOverrideValueFormatter::ToString(actual->primary), ScenePrefabOverrideFlag::Camera);
    }
}

} // namespace kb::scene
