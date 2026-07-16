#include "scene/prefab/ScenePrefabAppliedPropertyBuilder.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <sstream>
#include <string>

namespace kb::scene {
namespace {

[[nodiscard]] bool StartsWith(std::string_view value, std::string_view prefix) noexcept {
    return value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string ToString(bool value) {
    return value ? "true" : "false";
}

[[nodiscard]] std::string ToString(Vec3 value) {
    return std::to_string(value.x) + " " + std::to_string(value.y) + " " + std::to_string(value.z);
}

[[nodiscard]] std::string ToString(Quat value) {
    return std::to_string(value.x) + " " + std::to_string(value.y) + " " + std::to_string(value.z) + " " + std::to_string(value.w);
}

[[nodiscard]] ScenePrefabOverrideFlag FlagForProperty(std::string_view propertyPath) noexcept {
    if (propertyPath == "name") {
        return ScenePrefabOverrideFlag::Name;
    }
    if (propertyPath == "parent") {
        return ScenePrefabOverrideFlag::Parent;
    }
    if (StartsWith(propertyPath, "transform.")) {
        return ScenePrefabOverrideFlag::Transform;
    }
    if (propertyPath == "visibility.visible") {
        return ScenePrefabOverrideFlag::Visibility;
    }
    if (StartsWith(propertyPath, "camera")) {
        return ScenePrefabOverrideFlag::Camera;
    }
    if (StartsWith(propertyPath, "meshRenderer")) {
        return ScenePrefabOverrideFlag::MeshRenderer;
    }
    if (StartsWith(propertyPath, "light")) {
        return ScenePrefabOverrideFlag::Light;
    }
    if (StartsWith(propertyPath, "input")) {
        return ScenePrefabOverrideFlag::Input;
    }
    if (StartsWith(propertyPath, "rigidbody")) {
        return ScenePrefabOverrideFlag::Rigidbody;
    }
    if (StartsWith(propertyPath, "collider")) {
        return ScenePrefabOverrideFlag::Collider;
    }
    if (StartsWith(propertyPath, "tags")) {
        return ScenePrefabOverrideFlag::Tags;
    }
    if (StartsWith(propertyPath, "behaviour")) {
        return ScenePrefabOverrideFlag::Behaviour;
    }
    if (StartsWith(propertyPath, "audioSource")) {
        return ScenePrefabOverrideFlag::AudioSource;
    }
    if (StartsWith(propertyPath, "audioListener")) {
        return ScenePrefabOverrideFlag::AudioListener;
    }
    if (propertyPath == "children") {
        return ScenePrefabOverrideFlag::AddedChild;
    }
    return ScenePrefabOverrideFlag::None;
}

} // namespace

bool ScenePrefabAppliedPropertyBuilder::Build(Scene& scene, std::uint32_t nodeIndex, SceneObject object, std::string_view propertyPath, ScenePrefabPropertyOverride& property) {
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return false;
    }

    property = ScenePrefabPropertyOverride{
        .nodeIndex = nodeIndex,
        .nodeId = 0,
        .target = object,
        .propertyPath = std::string{ propertyPath },
        .value = {},
        .objectReference = {},
        .flag = FlagForProperty(propertyPath),
    };
    if (propertyPath == "name") {
        property.value = scene.Entities().Name(object);
        return true;
    }
    if (StartsWith(propertyPath, "transform.")) {
        const TransformComponent transform = scene.Transforms().Get(object);
        if (propertyPath == "transform.localPosition") {
            property.value = ToString(transform.localPosition);
            return true;
        }
        if (propertyPath == "transform.localRotation") {
            property.value = ToString(transform.localRotation);
            return true;
        }
        if (propertyPath == "transform.localScale") {
            property.value = ToString(transform.localScale);
            return true;
        }
    }
    if (propertyPath == "visibility.visible") {
        property.value = ToString(scene.Components().Visibility().Get(object.Entity()).visible);
        return true;
    }
    SceneComponents components = scene.Components();
    const SceneEntity entity = object.Entity();
    if (StartsWith(propertyPath, "camera")) {
        const CameraComponent* camera = components.Cameras().TryGet(entity);
        if (propertyPath == "camera") {
            property.value = camera == nullptr ? "null" : "present";
            return true;
        }
        if (camera == nullptr) {
            return false;
        }
        if (propertyPath == "camera.projection") {
            property.value = std::to_string(static_cast<int>(camera->projection));
            return true;
        }
        if (propertyPath == "camera.verticalFovDegrees") {
            property.value = std::to_string(camera->verticalFovDegrees);
            return true;
        }
        if (propertyPath == "camera.orthographicHeight") {
            property.value = std::to_string(camera->orthographicHeight);
            return true;
        }
        if (propertyPath == "camera.nearClip") {
            property.value = std::to_string(camera->nearClip);
            return true;
        }
        if (propertyPath == "camera.farClip") {
            property.value = std::to_string(camera->farClip);
            return true;
        }
        if (propertyPath == "camera.primary") {
            property.value = ToString(camera->primary);
            return true;
        }
        if (propertyPath == "camera.viewportId") {
            property.value = std::to_string(camera->viewportId);
            return true;
        }
        if (propertyPath == "camera.priority") {
            property.value = std::to_string(camera->priority);
            return true;
        }
        if (propertyPath == "camera.cullingMask") {
            property.value = std::to_string(camera->cullingMask);
            return true;
        }
        if (propertyPath == "camera.clearMode") {
            property.value = std::to_string(static_cast<int>(camera->clearMode));
            return true;
        }
        if (propertyPath == "camera.clearColor") {
            property.value = ToString(camera->clearColor);
            return true;
        }
    }
    if (StartsWith(propertyPath, "meshRenderer")) {
        const MeshRendererComponent* meshRenderer = components.MeshRenderers().TryGet(entity);
        if (propertyPath == "meshRenderer") {
            property.value = meshRenderer == nullptr ? "null" : "present";
            return true;
        }
        if (meshRenderer == nullptr) {
            return false;
        }
        if (propertyPath == "meshRenderer.meshAssetId") {
            property.value = std::to_string(meshRenderer->meshAssetId);
            return true;
        }
        if (propertyPath == "meshRenderer.materialAssetId") {
            property.value = std::to_string(meshRenderer->materialAssetId);
            return true;
        }
        if (propertyPath == "meshRenderer.materialSlotOverrideCount") {
            property.value = std::to_string(meshRenderer->materialSlotOverrideCount);
            return true;
        }
        if (StartsWith(propertyPath, "meshRenderer.materialSlotAssetId.")) {
            const std::string indexText{ propertyPath.substr(std::string_view{ "meshRenderer.materialSlotAssetId." }.size()) };
            std::uint32_t slotIndex = 0;
            std::istringstream stream{ indexText };
            if (!(stream >> slotIndex) || slotIndex >= kMaxMeshRendererMaterialSlotOverrides) {
                return false;
            }
            property.value = std::to_string(meshRenderer->materialSlotAssetIds[slotIndex]);
            return true;
        }
        if (propertyPath == "meshRenderer.castsShadow") {
            property.value = ToString(meshRenderer->castsShadow);
            return true;
        }
        if (propertyPath == "meshRenderer.receivesShadow") {
            property.value = ToString(meshRenderer->receivesShadow);
            return true;
        }
        if (propertyPath == "meshRenderer.layer") {
            property.value = std::to_string(meshRenderer->layer);
            return true;
        }
    }
    if (StartsWith(propertyPath, "light")) {
        const LightComponent* light = components.Lights().TryGet(entity);
        if (propertyPath == "light") {
            property.value = light == nullptr ? "null" : "present";
            return true;
        }
        if (light == nullptr) {
            return false;
        }
        if (propertyPath == "light.kind") {
            property.value = std::to_string(static_cast<int>(light->kind));
            return true;
        }
        if (propertyPath == "light.color") {
            property.value = ToString(light->color);
            return true;
        }
        if (propertyPath == "light.intensity") {
            property.value = std::to_string(light->intensity);
            return true;
        }
        if (propertyPath == "light.range") {
            property.value = std::to_string(light->range);
            return true;
        }
        if (propertyPath == "light.innerConeDegrees") {
            property.value = std::to_string(light->innerConeDegrees);
            return true;
        }
        if (propertyPath == "light.outerConeDegrees") {
            property.value = std::to_string(light->outerConeDegrees);
            return true;
        }
        if (propertyPath == "light.areaWidth") {
            property.value = std::to_string(light->areaWidth);
            return true;
        }
        if (propertyPath == "light.areaHeight") {
            property.value = std::to_string(light->areaHeight);
            return true;
        }
        if (propertyPath == "light.contactShadowLength") {
            property.value = std::to_string(light->contactShadowLength);
            return true;
        }
        if (propertyPath == "light.volumetricScattering") {
            property.value = std::to_string(light->volumetricScattering);
            return true;
        }
        if (propertyPath == "light.castsShadow") {
            property.value = ToString(light->castsShadow);
            return true;
        }
        if (propertyPath == "light.useColorTemperature") {
            property.value = ToString(light->useColorTemperature);
            return true;
        }
        if (propertyPath == "light.colorTemperatureKelvin") {
            property.value = std::to_string(light->colorTemperatureKelvin);
            return true;
        }
        if (propertyPath == "light.layerMask") {
            property.value = std::to_string(light->layerMask);
            return true;
        }
    }
    if (StartsWith(propertyPath, "input")) {
        const InputComponent* input = components.Inputs().TryGet(entity);
        if (propertyPath == "input") {
            property.value = input == nullptr ? "null" : "present";
            return true;
        }
        if (input == nullptr) {
            return false;
        }
        if (propertyPath == "input.mappingContextAssetId") {
            property.value = std::to_string(input->mappingContextAssetId);
            return true;
        }
        if (propertyPath == "input.priority") {
            property.value = std::to_string(input->priority);
            return true;
        }
        if (propertyPath == "input.enabled") {
            property.value = ToString(input->enabled);
            return true;
        }
        if (propertyPath == "input.localUser") {
            property.value = std::to_string(input->localUser.value);
            return true;
        }
    }
    if (StartsWith(propertyPath, "rigidbody")) {
        const RigidbodyComponent* rigidbody = components.Rigidbodies().TryGet(entity);
        if (propertyPath == "rigidbody") {
            property.value = rigidbody == nullptr ? "null" : "present";
            return true;
        }
        if (rigidbody == nullptr) {
            return false;
        }
        if (propertyPath == "rigidbody.bodyType") {
            property.value = std::to_string(static_cast<int>(rigidbody->bodyType));
            return true;
        }
        if (propertyPath == "rigidbody.mass") {
            property.value = std::to_string(rigidbody->mass);
            return true;
        }
        if (propertyPath == "rigidbody.linearVelocity") {
            property.value = ToString(rigidbody->linearVelocity);
            return true;
        }
        if (propertyPath == "rigidbody.angularVelocity") {
            property.value = ToString(rigidbody->angularVelocity);
            return true;
        }
        if (propertyPath == "rigidbody.gravityScale") {
            property.value = std::to_string(rigidbody->gravityScale);
            return true;
        }
        if (propertyPath == "rigidbody.useGravity") {
            property.value = ToString(rigidbody->useGravity);
            return true;
        }
        if (propertyPath == "rigidbody.lockRotation") {
            property.value = ToString(rigidbody->lockRotation);
            return true;
        }
    }
    if (StartsWith(propertyPath, "collider")) {
        const ColliderComponent* collider = components.Colliders().TryGet(entity);
        if (propertyPath == "collider") {
            property.value = collider == nullptr ? "null" : "present";
            return true;
        }
        if (collider == nullptr) {
            return false;
        }
        if (propertyPath == "collider.shape") {
            property.value = std::to_string(static_cast<int>(collider->shape));
            return true;
        }
        if (propertyPath == "collider.center") {
            property.value = ToString(collider->center);
            return true;
        }
        if (propertyPath == "collider.boxSize") {
            property.value = ToString(collider->boxSize);
            return true;
        }
        if (propertyPath == "collider.radius") {
            property.value = std::to_string(collider->radius);
            return true;
        }
        if (propertyPath == "collider.height") {
            property.value = std::to_string(collider->height);
            return true;
        }
        if (propertyPath == "collider.trigger") {
            property.value = ToString(collider->trigger);
            return true;
        }
    }
    if (StartsWith(propertyPath, "tags")) {
        const TagsComponent* tags = components.Tags().TryGet(entity);
        if (propertyPath == "tags") {
            property.value = tags == nullptr ? "null" : "present";
            return true;
        }
        if (tags != nullptr && propertyPath == "tags.value") {
            property.value = TagsText(*tags);
            return true;
        }
        return false;
    }
    if (StartsWith(propertyPath, "behaviour")) {
        const BehaviourComponent* behaviour = components.Behaviours().TryGet(entity);
        if (propertyPath == "behaviour") {
            property.value = behaviour == nullptr ? "null" : "present";
            return true;
        }
        if (behaviour == nullptr) {
            return false;
        }
        if (propertyPath == "behaviour.behaviourAssetId") {
            property.value = std::to_string(behaviour->behaviourAssetId);
            return true;
        }
        if (propertyPath == "behaviour.backend") {
            property.value = std::to_string(static_cast<int>(behaviour->backend));
            return true;
        }
        if (propertyPath == "behaviour.enabled") {
            property.value = ToString(behaviour->enabled);
            return true;
        }
        if (propertyPath == "behaviour.tickGroup") {
            property.value = std::to_string(static_cast<int>(behaviour->tickGroup));
            return true;
        }
        if (propertyPath == "behaviour.executionOrder") {
            property.value = std::to_string(behaviour->executionOrder);
            return true;
        }
    }
    if (StartsWith(propertyPath, "audioSource")) {
        const AudioSourceComponent* audioSource = components.AudioSources().TryGet(entity);
        if (propertyPath == "audioSource") {
            property.value = audioSource == nullptr ? "null" : "present";
            return true;
        }
        if (audioSource == nullptr) {
            return false;
        }
        if (propertyPath == "audioSource.clipAssetId") {
            property.value = std::to_string(audioSource->clipAssetId);
            return true;
        }
        if (propertyPath == "audioSource.volume") {
            property.value = std::to_string(audioSource->volume);
            return true;
        }
        if (propertyPath == "audioSource.pitch") {
            property.value = std::to_string(audioSource->pitch);
            return true;
        }
        if (propertyPath == "audioSource.loop") {
            property.value = ToString(audioSource->loop);
            return true;
        }
        if (propertyPath == "audioSource.spatial") {
            property.value = ToString(audioSource->spatial);
            return true;
        }
        if (propertyPath == "audioSource.autoplay") {
            property.value = ToString(audioSource->autoplay);
            return true;
        }
        if (propertyPath == "audioSource.enabled") {
            property.value = ToString(audioSource->enabled);
            return true;
        }
        if (propertyPath == "audioSource.mute") {
            property.value = ToString(audioSource->mute);
            return true;
        }
        if (propertyPath == "audioSource.pan") {
            property.value = std::to_string(audioSource->pan);
            return true;
        }
        if (propertyPath == "audioSource.spatialBlend") {
            property.value = std::to_string(audioSource->spatialBlend);
            return true;
        }
        if (propertyPath == "audioSource.attenuationModel") {
            property.value = std::to_string(static_cast<int>(audioSource->attenuationModel));
            return true;
        }
        if (propertyPath == "audioSource.minDistance") {
            property.value = std::to_string(audioSource->minDistance);
            return true;
        }
        if (propertyPath == "audioSource.maxDistance") {
            property.value = std::to_string(audioSource->maxDistance);
            return true;
        }
        if (propertyPath == "audioSource.rolloff") {
            property.value = std::to_string(audioSource->rolloff);
            return true;
        }
        if (propertyPath == "audioSource.dopplerFactor") {
            property.value = std::to_string(audioSource->dopplerFactor);
            return true;
        }
    }
    if (StartsWith(propertyPath, "audioListener")) {
        const AudioListenerComponent* audioListener = components.AudioListeners().TryGet(entity);
        if (propertyPath == "audioListener") {
            property.value = audioListener == nullptr ? "null" : "present";
            return true;
        }
        if (audioListener == nullptr) {
            return false;
        }
        if (propertyPath == "audioListener.primary") {
            property.value = ToString(audioListener->primary);
            return true;
        }
        if (propertyPath == "audioListener.enabled") {
            property.value = ToString(audioListener->enabled);
            return true;
        }
    }
    return false;
}

} // namespace kb::scene
