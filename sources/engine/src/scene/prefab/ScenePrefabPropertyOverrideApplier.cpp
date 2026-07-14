#include "scene/prefab/ScenePrefabPropertyOverrideApplier.hpp"

#include <charconv>
#include <sstream>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace kb::scene {
namespace {

[[nodiscard]] bool ParseVec3(std::string_view value, Vec3& output) {
    std::istringstream stream{ std::string{ value } };
    std::string extra;
    return static_cast<bool>(stream >> output.x >> output.y >> output.z) && !(stream >> extra);
}

[[nodiscard]] bool ParseQuat(std::string_view value, Quat& output) {
    std::istringstream stream{ std::string{ value } };
    std::string extra;
    return static_cast<bool>(stream >> output.x >> output.y >> output.z >> output.w) && !(stream >> extra);
}

[[nodiscard]] bool ParseBool(std::string_view value, bool& output) noexcept {
    if (value == "true" || value == "1") {
        output = true;
        return true;
    }
    if (value == "false" || value == "0") {
        output = false;
        return true;
    }
    return false;
}

template <typename T>
[[nodiscard]] bool ParseNumber(std::string_view text, T& output) {
    if constexpr (std::is_floating_point_v<T>) {
        std::istringstream stream{ std::string{ text } };
        std::string extra;
        return static_cast<bool>(stream >> output) && !(stream >> extra);
    } else {
        const char* first = text.data();
        const char* last = text.data() + text.size();
        const std::from_chars_result result = std::from_chars(first, last, output);
        return result.ec == std::errc{} && result.ptr == last;
    }
}

[[nodiscard]] bool ApplyComponentPresence(std::string_view value, auto& component) {
    if (value == "null") {
        component.reset();
        return true;
    }
    if (value == "present") {
        if (!component.has_value()) {
            component.emplace();
        }
        return true;
    }
    return false;
}

template <typename T>
[[nodiscard]] T& Ensure(std::optional<T>& component) {
    if (!component.has_value()) {
        component.emplace();
    }
    return *component;
}

} // namespace

bool ScenePrefabPropertyOverrideApplier::Apply(ScenePrefabNodeDesc& node, const ScenePrefabPropertyOverride& property) {
    if (property.propertyPath == "name") {
        node.name = property.value;
        return true;
    }
    if (property.propertyPath == "transform.localPosition") {
        return ParseVec3(property.value, node.transform.localPosition);
    }
    if (property.propertyPath == "transform.localRotation") {
        return ParseQuat(property.value, node.transform.localRotation);
    }
    if (property.propertyPath == "transform.localScale") {
        return ParseVec3(property.value, node.transform.localScale);
    }
    if (property.propertyPath == "visibility.visible") {
        return ParseBool(property.value, node.visibility.visible);
    }
    if (property.propertyPath == "camera") {
        return ApplyComponentPresence(property.value, node.components.camera);
    }
    if (property.propertyPath == "camera.projection") {
        int value = 0;
        if (!ParseNumber(property.value, value) || value < static_cast<int>(CameraProjection::Perspective) || value > static_cast<int>(CameraProjection::Orthographic)) {
            return false;
        }
        Ensure(node.components.camera).projection = static_cast<CameraProjection>(value);
        return true;
    }
    if (property.propertyPath == "camera.verticalFovDegrees") {
        return ParseNumber(property.value, Ensure(node.components.camera).verticalFovDegrees);
    }
    if (property.propertyPath == "camera.orthographicHeight") {
        return ParseNumber(property.value, Ensure(node.components.camera).orthographicHeight);
    }
    if (property.propertyPath == "camera.nearClip") {
        return ParseNumber(property.value, Ensure(node.components.camera).nearClip);
    }
    if (property.propertyPath == "camera.farClip") {
        return ParseNumber(property.value, Ensure(node.components.camera).farClip);
    }
    if (property.propertyPath == "camera.primary") {
        return ParseBool(property.value, Ensure(node.components.camera).primary);
    }
    if (property.propertyPath == "meshRenderer") {
        return ApplyComponentPresence(property.value, node.components.meshRenderer);
    }
    if (property.propertyPath == "meshRenderer.meshAssetId") {
        return ParseNumber(property.value, Ensure(node.components.meshRenderer).meshAssetId);
    }
    if (property.propertyPath == "meshRenderer.materialAssetId") {
        return ParseNumber(property.value, Ensure(node.components.meshRenderer).materialAssetId);
    }
    if (property.propertyPath == "meshRenderer.materialSlotOverrideCount") {
        std::uint32_t value = 0;
        if (!ParseNumber(property.value, value) || value > kMaxMeshRendererMaterialSlotOverrides) {
            return false;
        }
        Ensure(node.components.meshRenderer).materialSlotOverrideCount = value;
        return true;
    }
    if (property.propertyPath.rfind("meshRenderer.materialSlotAssetId.", 0) == 0) {
        std::uint32_t slotIndex = 0;
        const std::string indexText = property.propertyPath.substr(std::string_view{ "meshRenderer.materialSlotAssetId." }.size());
        if (!ParseNumber(indexText, slotIndex) || slotIndex >= kMaxMeshRendererMaterialSlotOverrides) {
            return false;
        }
        return ParseNumber(property.value, Ensure(node.components.meshRenderer).materialSlotAssetIds[slotIndex]);
    }
    if (property.propertyPath == "meshRenderer.materialSlotAssetId") {
        return ParseNumber(property.value, Ensure(node.components.meshRenderer).materialSlotAssetIds[0]);
    }
    if (property.propertyPath == "meshRenderer.castsShadow") {
        return ParseBool(property.value, Ensure(node.components.meshRenderer).castsShadow);
    }
    if (property.propertyPath == "meshRenderer.receivesShadow") {
        return ParseBool(property.value, Ensure(node.components.meshRenderer).receivesShadow);
    }
    if (property.propertyPath == "light") {
        return ApplyComponentPresence(property.value, node.components.light);
    }
    if (property.propertyPath == "light.kind") {
        int value = 0;
        if (!ParseNumber(property.value, value) || value < static_cast<int>(LightKind::Directional) || value > static_cast<int>(LightKind::Tube)) {
            return false;
        }
        Ensure(node.components.light).kind = static_cast<LightKind>(value);
        return true;
    }
    if (property.propertyPath == "light.color") {
        return ParseVec3(property.value, Ensure(node.components.light).color);
    }
    if (property.propertyPath == "light.intensity") {
        return ParseNumber(property.value, Ensure(node.components.light).intensity);
    }
    if (property.propertyPath == "light.range") {
        return ParseNumber(property.value, Ensure(node.components.light).range);
    }
    if (property.propertyPath == "light.innerConeDegrees") {
        return ParseNumber(property.value, Ensure(node.components.light).innerConeDegrees);
    }
    if (property.propertyPath == "light.outerConeDegrees") {
        return ParseNumber(property.value, Ensure(node.components.light).outerConeDegrees);
    }
    if (property.propertyPath == "light.areaWidth") {
        return ParseNumber(property.value, Ensure(node.components.light).areaWidth);
    }
    if (property.propertyPath == "light.areaHeight") {
        return ParseNumber(property.value, Ensure(node.components.light).areaHeight);
    }
    if (property.propertyPath == "light.contactShadowLength") {
        return ParseNumber(property.value, Ensure(node.components.light).contactShadowLength);
    }
    if (property.propertyPath == "light.volumetricScattering") {
        return ParseNumber(property.value, Ensure(node.components.light).volumetricScattering);
    }
    if (property.propertyPath == "light.castsShadow") {
        return ParseBool(property.value, Ensure(node.components.light).castsShadow);
    }
    if (property.propertyPath == "input") {
        return ApplyComponentPresence(property.value, node.components.input);
    }
    if (property.propertyPath == "input.mappingContextAssetId") {
        return ParseNumber(property.value, Ensure(node.components.input).mappingContextAssetId);
    }
    if (property.propertyPath == "input.priority") {
        return ParseNumber(property.value, Ensure(node.components.input).priority);
    }
    if (property.propertyPath == "input.enabled") {
        return ParseBool(property.value, Ensure(node.components.input).enabled);
    }
    if (property.propertyPath == "input.localUser") {
        return ParseNumber(property.value, Ensure(node.components.input).localUser.value);
    }
    if (property.propertyPath == "rigidbody") {
        return ApplyComponentPresence(property.value, node.components.rigidbody);
    }
    if (property.propertyPath == "rigidbody.bodyType") {
        int value = 0;
        if (!ParseNumber(property.value, value) || value < static_cast<int>(RigidbodyBodyType::Static) || value > static_cast<int>(RigidbodyBodyType::Kinematic)) {
            return false;
        }
        Ensure(node.components.rigidbody).bodyType = static_cast<RigidbodyBodyType>(value);
        return true;
    }
    if (property.propertyPath == "rigidbody.mass") {
        return ParseNumber(property.value, Ensure(node.components.rigidbody).mass);
    }
    if (property.propertyPath == "rigidbody.linearVelocity") {
        return ParseVec3(property.value, Ensure(node.components.rigidbody).linearVelocity);
    }
    if (property.propertyPath == "rigidbody.angularVelocity") {
        return ParseVec3(property.value, Ensure(node.components.rigidbody).angularVelocity);
    }
    if (property.propertyPath == "rigidbody.gravityScale") {
        return ParseNumber(property.value, Ensure(node.components.rigidbody).gravityScale);
    }
    if (property.propertyPath == "rigidbody.useGravity") {
        return ParseBool(property.value, Ensure(node.components.rigidbody).useGravity);
    }
    if (property.propertyPath == "rigidbody.lockRotation") {
        return ParseBool(property.value, Ensure(node.components.rigidbody).lockRotation);
    }
    if (property.propertyPath == "collider") {
        return ApplyComponentPresence(property.value, node.components.collider);
    }
    if (property.propertyPath == "collider.shape") {
        int value = 0;
        if (!ParseNumber(property.value, value) || value < static_cast<int>(ColliderShape::Box) || value > static_cast<int>(ColliderShape::Capsule)) {
            return false;
        }
        Ensure(node.components.collider).shape = static_cast<ColliderShape>(value);
        return true;
    }
    if (property.propertyPath == "collider.center") {
        return ParseVec3(property.value, Ensure(node.components.collider).center);
    }
    if (property.propertyPath == "collider.boxSize") {
        return ParseVec3(property.value, Ensure(node.components.collider).boxSize);
    }
    if (property.propertyPath == "collider.radius") {
        return ParseNumber(property.value, Ensure(node.components.collider).radius);
    }
    if (property.propertyPath == "collider.height") {
        return ParseNumber(property.value, Ensure(node.components.collider).height);
    }
    if (property.propertyPath == "collider.trigger") {
        return ParseBool(property.value, Ensure(node.components.collider).trigger);
    }
    if (property.propertyPath == "tags") {
        return ApplyComponentPresence(property.value, node.components.tags);
    }
    if (property.propertyPath == "tags.value") {
        SetTagsText(Ensure(node.components.tags), property.value);
        return true;
    }
    if (property.propertyPath == "behaviour") {
        return ApplyComponentPresence(property.value, node.components.behaviour);
    }
    if (property.propertyPath == "behaviour.behaviourAssetId") {
        return ParseNumber(property.value, Ensure(node.components.behaviour).behaviourAssetId);
    }
    if (property.propertyPath == "behaviour.backend") {
        int value = 0;
        if (!ParseNumber(property.value, value) || value < static_cast<int>(BehaviourBackend::Native) || value > static_cast<int>(BehaviourBackend::VisualGraph)) {
            return false;
        }
        Ensure(node.components.behaviour).backend = static_cast<BehaviourBackend>(value);
        return true;
    }
    if (property.propertyPath == "behaviour.enabled") {
        return ParseBool(property.value, Ensure(node.components.behaviour).enabled);
    }
    if (property.propertyPath == "behaviour.tickGroup") {
        int value = 0;
        if (!ParseNumber(property.value, value) || value < static_cast<int>(BehaviourTickGroup::Input) || value > static_cast<int>(BehaviourTickGroup::Presentation)) {
            return false;
        }
        Ensure(node.components.behaviour).tickGroup = static_cast<BehaviourTickGroup>(value);
        return true;
    }
    if (property.propertyPath == "behaviour.executionOrder") {
        return ParseNumber(property.value, Ensure(node.components.behaviour).executionOrder);
    }
    if (property.propertyPath == "audioSource") {
        return ApplyComponentPresence(property.value, node.components.audioSource);
    }
    if (property.propertyPath == "audioSource.clipAssetId") {
        return ParseNumber(property.value, Ensure(node.components.audioSource).clipAssetId);
    }
    if (property.propertyPath == "audioSource.volume") {
        return ParseNumber(property.value, Ensure(node.components.audioSource).volume);
    }
    if (property.propertyPath == "audioSource.pitch") {
        return ParseNumber(property.value, Ensure(node.components.audioSource).pitch);
    }
    if (property.propertyPath == "audioSource.loop") {
        return ParseBool(property.value, Ensure(node.components.audioSource).loop);
    }
    if (property.propertyPath == "audioSource.spatial") {
        return ParseBool(property.value, Ensure(node.components.audioSource).spatial);
    }
    if (property.propertyPath == "audioSource.autoplay") {
        return ParseBool(property.value, Ensure(node.components.audioSource).autoplay);
    }
    if (property.propertyPath == "audioSource.enabled") {
        return ParseBool(property.value, Ensure(node.components.audioSource).enabled);
    }
    if (property.propertyPath == "audioSource.mute") {
        return ParseBool(property.value, Ensure(node.components.audioSource).mute);
    }
    if (property.propertyPath == "audioSource.pan") {
        return ParseNumber(property.value, Ensure(node.components.audioSource).pan);
    }
    if (property.propertyPath == "audioSource.spatialBlend") {
        return ParseNumber(property.value, Ensure(node.components.audioSource).spatialBlend);
    }
    if (property.propertyPath == "audioSource.attenuationModel") {
        int value = 0;
        if (!ParseNumber(property.value, value)
            || value < static_cast<int>(kb::audio::AudioAttenuationModel::None)
            || value > static_cast<int>(kb::audio::AudioAttenuationModel::Exponential)) {
            return false;
        }
        Ensure(node.components.audioSource).attenuationModel = static_cast<kb::audio::AudioAttenuationModel>(value);
        return true;
    }
    if (property.propertyPath == "audioSource.minDistance") {
        return ParseNumber(property.value, Ensure(node.components.audioSource).minDistance);
    }
    if (property.propertyPath == "audioSource.maxDistance") {
        return ParseNumber(property.value, Ensure(node.components.audioSource).maxDistance);
    }
    if (property.propertyPath == "audioSource.rolloff") {
        return ParseNumber(property.value, Ensure(node.components.audioSource).rolloff);
    }
    if (property.propertyPath == "audioSource.dopplerFactor") {
        return ParseNumber(property.value, Ensure(node.components.audioSource).dopplerFactor);
    }
    if (property.propertyPath == "audioListener") {
        return ApplyComponentPresence(property.value, node.components.audioListener);
    }
    if (property.propertyPath == "audioListener.primary") {
        return ParseBool(property.value, Ensure(node.components.audioListener).primary);
    }
    if (property.propertyPath == "audioListener.enabled") {
        return ParseBool(property.value, Ensure(node.components.audioListener).enabled);
    }
    return false;
}

} // namespace kb::scene
