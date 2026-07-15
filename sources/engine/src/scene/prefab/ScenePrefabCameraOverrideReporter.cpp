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
        && lhs.primary == rhs.primary
        && lhs.viewportId == rhs.viewportId
        && lhs.priority == rhs.priority
        && lhs.cullingMask == rhs.cullingMask
        && lhs.clearMode == rhs.clearMode
        && lhs.clearColor.x == rhs.clearColor.x
        && lhs.clearColor.y == rhs.clearColor.y
        && lhs.clearColor.z == rhs.clearColor.z;
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
    if (actual == nullptr) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera", "null", ScenePrefabOverrideFlag::Camera);
        return;
    }
    if (!expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera", "present", ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.projection", std::to_string(static_cast<int>(actual->projection)), ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.verticalFovDegrees", ScenePrefabOverrideValueFormatter::ToString(actual->verticalFovDegrees), ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.orthographicHeight", ScenePrefabOverrideValueFormatter::ToString(actual->orthographicHeight), ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.nearClip", ScenePrefabOverrideValueFormatter::ToString(actual->nearClip), ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.farClip", ScenePrefabOverrideValueFormatter::ToString(actual->farClip), ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.primary", ScenePrefabOverrideValueFormatter::ToString(actual->primary), ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.viewportId", std::to_string(actual->viewportId), ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.priority", std::to_string(actual->priority), ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.cullingMask", std::to_string(actual->cullingMask), ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.clearMode", std::to_string(static_cast<int>(actual->clearMode)), ScenePrefabOverrideFlag::Camera);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.clearColor", ScenePrefabOverrideValueFormatter::ToString(actual->clearColor), ScenePrefabOverrideFlag::Camera);
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
    if (actual->viewportId != expected->viewportId) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.viewportId", std::to_string(actual->viewportId), ScenePrefabOverrideFlag::Camera);
    }
    if (actual->priority != expected->priority) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.priority", std::to_string(actual->priority), ScenePrefabOverrideFlag::Camera);
    }
    if (actual->cullingMask != expected->cullingMask) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.cullingMask", std::to_string(actual->cullingMask), ScenePrefabOverrideFlag::Camera);
    }
    if (actual->clearMode != expected->clearMode) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.clearMode", std::to_string(static_cast<int>(actual->clearMode)), ScenePrefabOverrideFlag::Camera);
    }
    if (actual->clearColor.x != expected->clearColor.x || actual->clearColor.y != expected->clearColor.y || actual->clearColor.z != expected->clearColor.z) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "camera.clearColor", ScenePrefabOverrideValueFormatter::ToString(actual->clearColor), ScenePrefabOverrideFlag::Camera);
    }
}

} // namespace kb::scene
