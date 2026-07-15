#include "scene/prefab/io/ScenePrefabAssetCameraParser.hpp"

#include "engine/scene/SceneComponents.hpp"

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool ParseField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator != fields.end() && ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
}

} // namespace

bool ScenePrefabAssetCameraParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasCamera = false;
    if (!ScenePrefabAssetFieldParser::ParseBool(fields, "camera", hasCamera)) {
        return false;
    }
    if (!hasCamera) {
        return true;
    }

    int projection = 0;
    CameraComponent camera;
    bool primary = false;
    if (!ParseField(fields, "camera.projection", projection)
        || projection < static_cast<int>(CameraProjection::Perspective)
        || projection > static_cast<int>(CameraProjection::Orthographic)
        || !ParseField(fields, "camera.verticalFovDegrees", camera.verticalFovDegrees)
        || !ParseField(fields, "camera.orthographicHeight", camera.orthographicHeight)
        || !ParseField(fields, "camera.nearClip", camera.nearClip)
        || !ParseField(fields, "camera.farClip", camera.farClip)
        || !ScenePrefabAssetFieldParser::ParseBool(fields, "camera.primary", primary)
        || !ParseField(fields, "camera.viewportId", camera.viewportId)
        || !ParseField(fields, "camera.priority", camera.priority)) {
        return false;
    }

    camera.projection = static_cast<CameraProjection>(projection);
    camera.primary = primary;
    components.camera = camera;
    return true;
}

} // namespace kb::scene
