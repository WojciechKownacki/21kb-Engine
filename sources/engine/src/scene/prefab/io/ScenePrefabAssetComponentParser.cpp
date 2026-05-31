#include "scene/prefab/io/ScenePrefabAssetComponentParser.hpp"

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool ParseField(const ScenePrefabAssetFieldMap& fields, std::string_view key, T& output) {
    const auto iterator = fields.find(std::string{ key });
    return iterator != fields.end() && ScenePrefabAssetFieldParser::ParseNumber(iterator->second, output);
}

} // namespace

bool ScenePrefabAssetComponentParser::Parse(const ScenePrefabAssetFieldMap& fields, ScenePrefabNodeComponents& components) {
    bool hasCamera = false;
    if (!ScenePrefabAssetFieldParser::ParseBool(fields, "camera", hasCamera)) {
        return false;
    }
    if (hasCamera) {
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
            || !ScenePrefabAssetFieldParser::ParseBool(fields, "camera.primary", primary)) {
            return false;
        }
        camera.projection = static_cast<CameraProjection>(projection);
        camera.primary = primary;
        components.camera = camera;
    }

    bool hasMeshRenderer = false;
    if (!ScenePrefabAssetFieldParser::ParseBool(fields, "meshRenderer", hasMeshRenderer)) {
        return false;
    }
    if (hasMeshRenderer) {
        MeshRendererComponent meshRenderer;
        bool castsShadow = false;
        bool receivesShadow = false;
        if (!ParseField(fields, "meshRenderer.meshAssetId", meshRenderer.meshAssetId)
            || !ParseField(fields, "meshRenderer.materialAssetId", meshRenderer.materialAssetId)
            || !ScenePrefabAssetFieldParser::ParseBool(fields, "meshRenderer.castsShadow", castsShadow)
            || !ScenePrefabAssetFieldParser::ParseBool(fields, "meshRenderer.receivesShadow", receivesShadow)) {
            return false;
        }
        meshRenderer.castsShadow = castsShadow;
        meshRenderer.receivesShadow = receivesShadow;
        components.meshRenderer = meshRenderer;
    }

    bool hasLight = false;
    if (!ScenePrefabAssetFieldParser::ParseBool(fields, "light", hasLight)) {
        return false;
    }
    if (hasLight) {
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
    }

    return true;
}

} // namespace kb::scene
