#include "scene/prefab/io/ScenePrefabAssetComponentWriter.hpp"

#include "engine/scene/SceneTransforms.hpp"

#include <ostream>

namespace kb::scene {
namespace {

void WriteVec3(std::ostream& output, const char* key, Vec3 value) {
    output << key << '=' << value.x << ' ' << value.y << ' ' << value.z << '\n';
}

} // namespace

void ScenePrefabAssetComponentWriter::Write(std::ostream& output, const ScenePrefabNodeComponents& components) {
    output << "camera=" << (components.camera.has_value() ? 1 : 0) << '\n';
    if (components.camera.has_value()) {
        output << "camera.projection=" << static_cast<int>(components.camera->projection) << '\n';
        output << "camera.verticalFovDegrees=" << components.camera->verticalFovDegrees << '\n';
        output << "camera.orthographicHeight=" << components.camera->orthographicHeight << '\n';
        output << "camera.nearClip=" << components.camera->nearClip << '\n';
        output << "camera.farClip=" << components.camera->farClip << '\n';
        output << "camera.primary=" << (components.camera->primary ? 1 : 0) << '\n';
    }

    output << "meshRenderer=" << (components.meshRenderer.has_value() ? 1 : 0) << '\n';
    if (components.meshRenderer.has_value()) {
        output << "meshRenderer.meshAssetId=" << components.meshRenderer->meshAssetId << '\n';
        output << "meshRenderer.materialAssetId=" << components.meshRenderer->materialAssetId << '\n';
        output << "meshRenderer.castsShadow=" << (components.meshRenderer->castsShadow ? 1 : 0) << '\n';
        output << "meshRenderer.receivesShadow=" << (components.meshRenderer->receivesShadow ? 1 : 0) << '\n';
    }

    output << "light=" << (components.light.has_value() ? 1 : 0) << '\n';
    if (components.light.has_value()) {
        output << "light.kind=" << static_cast<int>(components.light->kind) << '\n';
        WriteVec3(output, "light.color", components.light->color);
        output << "light.intensity=" << components.light->intensity << '\n';
        output << "light.range=" << components.light->range << '\n';
        output << "light.innerConeDegrees=" << components.light->innerConeDegrees << '\n';
        output << "light.outerConeDegrees=" << components.light->outerConeDegrees << '\n';
    }
}

} // namespace kb::scene
