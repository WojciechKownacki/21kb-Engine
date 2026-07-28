#include "scene/prefab/ScenePrefabComponentOverrideReporter.hpp"

#include "scene/prefab/ScenePrefabCameraOverrideReporter.hpp"
#include "scene/prefab/ScenePrefabLightOverrideReporter.hpp"
#include "scene/prefab/ScenePrefabMeshRendererOverrideReporter.hpp"
#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"
#include "scene/prefab/ScenePrefabOverrideValueFormatter.hpp"

#include <string>

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] bool EqualOptional(const T* actual, const std::optional<T>& expected, bool (*equal)(const T&, const T&)) noexcept {
    if (actual == nullptr && !expected.has_value()) {
        return true;
    }
    return actual != nullptr && expected.has_value() && equal(*actual, *expected);
}

template <typename T>
[[nodiscard]] bool HasPresenceOverride(const T* actual, const std::optional<T>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object, std::string propertyPath, ScenePrefabOverrideFlag flag) {
    if (actual == nullptr && !expected.has_value()) {
        return true;
    }
    if (actual == nullptr) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, std::move(propertyPath), "null", flag);
        return true;
    }
    if (!expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, std::move(propertyPath), actual == nullptr ? "null" : "present", flag);
        return false;
    }
    return false;
}

[[nodiscard]] bool Equal(const InputComponent& lhs, const InputComponent& rhs) noexcept {
    return lhs.mappingContextAssetId == rhs.mappingContextAssetId
        && lhs.priority == rhs.priority
        && lhs.enabled == rhs.enabled
        && lhs.localUser == rhs.localUser;
}

[[nodiscard]] bool Equal(const RigidbodyComponent& lhs, const RigidbodyComponent& rhs) noexcept {
    return lhs.bodyType == rhs.bodyType
        && lhs.mass == rhs.mass
        && ScenePrefabOverrideValueFormatter::Equal(lhs.linearVelocity, rhs.linearVelocity)
        && ScenePrefabOverrideValueFormatter::Equal(lhs.angularVelocity, rhs.angularVelocity)
        && lhs.gravityScale == rhs.gravityScale
        && lhs.useGravity == rhs.useGravity
        && lhs.lockRotation == rhs.lockRotation;
}

[[nodiscard]] bool Equal(const ColliderComponent& lhs, const ColliderComponent& rhs) noexcept {
    return lhs.shape == rhs.shape
        && ScenePrefabOverrideValueFormatter::Equal(lhs.center, rhs.center)
        && ScenePrefabOverrideValueFormatter::Equal(lhs.boxSize, rhs.boxSize)
        && lhs.radius == rhs.radius
        && lhs.height == rhs.height
        && lhs.trigger == rhs.trigger;
}

[[nodiscard]] bool Equal(const TagsComponent& lhs, const TagsComponent& rhs) noexcept {
    return TagsText(lhs) == TagsText(rhs);
}

[[nodiscard]] bool Equal(const BehaviourComponent& lhs, const BehaviourComponent& rhs) noexcept {
    return lhs.behaviourAssetId == rhs.behaviourAssetId
        && lhs.backend == rhs.backend
        && lhs.enabled == rhs.enabled
        && lhs.tickGroup == rhs.tickGroup
        && lhs.executionOrder == rhs.executionOrder;
}

[[nodiscard]] bool Equal(const AudioSourceComponent& lhs, const AudioSourceComponent& rhs) noexcept {
    return lhs.clipAssetId == rhs.clipAssetId
        && lhs.volume == rhs.volume
        && lhs.pitch == rhs.pitch
        && lhs.loop == rhs.loop
        && lhs.spatial == rhs.spatial
        && lhs.autoplay == rhs.autoplay
        && lhs.enabled == rhs.enabled
        && lhs.mute == rhs.mute
        && lhs.pan == rhs.pan
        && lhs.spatialBlend == rhs.spatialBlend
        && lhs.attenuationModel == rhs.attenuationModel
        && lhs.minDistance == rhs.minDistance
        && lhs.maxDistance == rhs.maxDistance
        && lhs.rolloff == rhs.rolloff
        && lhs.dopplerFactor == rhs.dopplerFactor
        && AudioSourceOutputBus(lhs) == AudioSourceOutputBus(rhs);
}

[[nodiscard]] bool Equal(const AudioListenerComponent& lhs, const AudioListenerComponent& rhs) noexcept {
    return lhs.primary == rhs.primary
        && lhs.enabled == rhs.enabled;
}

void AppendInput(SceneComponents components, SceneEntity entity, const std::optional<InputComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const InputComponent* actual = components.Inputs().TryGet(entity);
    if (EqualOptional(actual, expected, &Equal)) {
        return;
    }
    if (HasPresenceOverride(actual, expected, report, nodeIndex, object, "input", ScenePrefabOverrideFlag::Input)) {
        return;
    }
    if (!expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "input.mappingContextAssetId", ScenePrefabOverrideValueFormatter::ToString(actual->mappingContextAssetId), ScenePrefabOverrideFlag::Input);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "input.priority", std::to_string(actual->priority), ScenePrefabOverrideFlag::Input);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "input.enabled", ScenePrefabOverrideValueFormatter::ToString(actual->enabled), ScenePrefabOverrideFlag::Input);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "input.localUser", std::to_string(actual->localUser.value), ScenePrefabOverrideFlag::Input);
        return;
    }
    if (actual->mappingContextAssetId != expected->mappingContextAssetId) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "input.mappingContextAssetId", ScenePrefabOverrideValueFormatter::ToString(actual->mappingContextAssetId), ScenePrefabOverrideFlag::Input);
    }
    if (actual->priority != expected->priority) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "input.priority", std::to_string(actual->priority), ScenePrefabOverrideFlag::Input);
    }
    if (actual->enabled != expected->enabled) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "input.enabled", ScenePrefabOverrideValueFormatter::ToString(actual->enabled), ScenePrefabOverrideFlag::Input);
    }
    if (actual->localUser != expected->localUser) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "input.localUser", std::to_string(actual->localUser.value), ScenePrefabOverrideFlag::Input);
    }
}

void AppendRigidbody(SceneComponents components, SceneEntity entity, const std::optional<RigidbodyComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const RigidbodyComponent* actual = components.Rigidbodies().TryGet(entity);
    if (EqualOptional(actual, expected, &Equal)) {
        return;
    }
    if (HasPresenceOverride(actual, expected, report, nodeIndex, object, "rigidbody", ScenePrefabOverrideFlag::Rigidbody)) {
        return;
    }
    if (!expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.bodyType", std::to_string(static_cast<int>(actual->bodyType)), ScenePrefabOverrideFlag::Rigidbody);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.mass", ScenePrefabOverrideValueFormatter::ToString(actual->mass), ScenePrefabOverrideFlag::Rigidbody);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.linearVelocity", ScenePrefabOverrideValueFormatter::ToString(actual->linearVelocity), ScenePrefabOverrideFlag::Rigidbody);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.angularVelocity", ScenePrefabOverrideValueFormatter::ToString(actual->angularVelocity), ScenePrefabOverrideFlag::Rigidbody);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.gravityScale", ScenePrefabOverrideValueFormatter::ToString(actual->gravityScale), ScenePrefabOverrideFlag::Rigidbody);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.useGravity", ScenePrefabOverrideValueFormatter::ToString(actual->useGravity), ScenePrefabOverrideFlag::Rigidbody);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.lockRotation", ScenePrefabOverrideValueFormatter::ToString(actual->lockRotation), ScenePrefabOverrideFlag::Rigidbody);
        return;
    }
    if (actual->bodyType != expected->bodyType) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.bodyType", std::to_string(static_cast<int>(actual->bodyType)), ScenePrefabOverrideFlag::Rigidbody);
    }
    if (actual->mass != expected->mass) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.mass", ScenePrefabOverrideValueFormatter::ToString(actual->mass), ScenePrefabOverrideFlag::Rigidbody);
    }
    if (!ScenePrefabOverrideValueFormatter::Equal(actual->linearVelocity, expected->linearVelocity)) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.linearVelocity", ScenePrefabOverrideValueFormatter::ToString(actual->linearVelocity), ScenePrefabOverrideFlag::Rigidbody);
    }
    if (!ScenePrefabOverrideValueFormatter::Equal(actual->angularVelocity, expected->angularVelocity)) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.angularVelocity", ScenePrefabOverrideValueFormatter::ToString(actual->angularVelocity), ScenePrefabOverrideFlag::Rigidbody);
    }
    if (actual->gravityScale != expected->gravityScale) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.gravityScale", ScenePrefabOverrideValueFormatter::ToString(actual->gravityScale), ScenePrefabOverrideFlag::Rigidbody);
    }
    if (actual->useGravity != expected->useGravity) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.useGravity", ScenePrefabOverrideValueFormatter::ToString(actual->useGravity), ScenePrefabOverrideFlag::Rigidbody);
    }
    if (actual->lockRotation != expected->lockRotation) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "rigidbody.lockRotation", ScenePrefabOverrideValueFormatter::ToString(actual->lockRotation), ScenePrefabOverrideFlag::Rigidbody);
    }
}

void AppendCollider(SceneComponents components, SceneEntity entity, const std::optional<ColliderComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const ColliderComponent* actual = components.Colliders().TryGet(entity);
    if (EqualOptional(actual, expected, &Equal)) {
        return;
    }
    if (HasPresenceOverride(actual, expected, report, nodeIndex, object, "collider", ScenePrefabOverrideFlag::Collider)) {
        return;
    }
    if (!expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.shape", std::to_string(static_cast<int>(actual->shape)), ScenePrefabOverrideFlag::Collider);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.center", ScenePrefabOverrideValueFormatter::ToString(actual->center), ScenePrefabOverrideFlag::Collider);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.boxSize", ScenePrefabOverrideValueFormatter::ToString(actual->boxSize), ScenePrefabOverrideFlag::Collider);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.radius", ScenePrefabOverrideValueFormatter::ToString(actual->radius), ScenePrefabOverrideFlag::Collider);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.height", ScenePrefabOverrideValueFormatter::ToString(actual->height), ScenePrefabOverrideFlag::Collider);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.trigger", ScenePrefabOverrideValueFormatter::ToString(actual->trigger), ScenePrefabOverrideFlag::Collider);
        return;
    }
    if (actual->shape != expected->shape) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.shape", std::to_string(static_cast<int>(actual->shape)), ScenePrefabOverrideFlag::Collider);
    }
    if (!ScenePrefabOverrideValueFormatter::Equal(actual->center, expected->center)) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.center", ScenePrefabOverrideValueFormatter::ToString(actual->center), ScenePrefabOverrideFlag::Collider);
    }
    if (!ScenePrefabOverrideValueFormatter::Equal(actual->boxSize, expected->boxSize)) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.boxSize", ScenePrefabOverrideValueFormatter::ToString(actual->boxSize), ScenePrefabOverrideFlag::Collider);
    }
    if (actual->radius != expected->radius) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.radius", ScenePrefabOverrideValueFormatter::ToString(actual->radius), ScenePrefabOverrideFlag::Collider);
    }
    if (actual->height != expected->height) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.height", ScenePrefabOverrideValueFormatter::ToString(actual->height), ScenePrefabOverrideFlag::Collider);
    }
    if (actual->trigger != expected->trigger) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "collider.trigger", ScenePrefabOverrideValueFormatter::ToString(actual->trigger), ScenePrefabOverrideFlag::Collider);
    }
}

void AppendTags(SceneComponents components, SceneEntity entity, const std::optional<TagsComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const TagsComponent* actual = components.Tags().TryGet(entity);
    if (EqualOptional(actual, expected, &Equal)) {
        return;
    }
    if (HasPresenceOverride(actual, expected, report, nodeIndex, object, "tags", ScenePrefabOverrideFlag::Tags)) {
        return;
    }
    if (!expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "tags.value", std::string{ TagsText(*actual) }, ScenePrefabOverrideFlag::Tags);
        return;
    }
    ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "tags.value", std::string{ TagsText(*actual) }, ScenePrefabOverrideFlag::Tags);
}

void AppendBehaviour(SceneComponents components, SceneEntity entity, const std::optional<BehaviourComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const BehaviourComponent* actual = components.Behaviours().TryGet(entity);
    if (EqualOptional(actual, expected, &Equal)) {
        return;
    }
    if (HasPresenceOverride(actual, expected, report, nodeIndex, object, "behaviour", ScenePrefabOverrideFlag::Behaviour)) {
        return;
    }
    if (!expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "behaviour.behaviourAssetId", ScenePrefabOverrideValueFormatter::ToString(actual->behaviourAssetId), ScenePrefabOverrideFlag::Behaviour);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "behaviour.backend", std::to_string(static_cast<int>(actual->backend)), ScenePrefabOverrideFlag::Behaviour);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "behaviour.enabled", ScenePrefabOverrideValueFormatter::ToString(actual->enabled), ScenePrefabOverrideFlag::Behaviour);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "behaviour.tickGroup", std::to_string(static_cast<int>(actual->tickGroup)), ScenePrefabOverrideFlag::Behaviour);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "behaviour.executionOrder", std::to_string(actual->executionOrder), ScenePrefabOverrideFlag::Behaviour);
        return;
    }
    if (actual->behaviourAssetId != expected->behaviourAssetId) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "behaviour.behaviourAssetId", ScenePrefabOverrideValueFormatter::ToString(actual->behaviourAssetId), ScenePrefabOverrideFlag::Behaviour);
    }
    if (actual->backend != expected->backend) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "behaviour.backend", std::to_string(static_cast<int>(actual->backend)), ScenePrefabOverrideFlag::Behaviour);
    }
    if (actual->enabled != expected->enabled) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "behaviour.enabled", ScenePrefabOverrideValueFormatter::ToString(actual->enabled), ScenePrefabOverrideFlag::Behaviour);
    }
    if (actual->tickGroup != expected->tickGroup) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "behaviour.tickGroup", std::to_string(static_cast<int>(actual->tickGroup)), ScenePrefabOverrideFlag::Behaviour);
    }
    if (actual->executionOrder != expected->executionOrder) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "behaviour.executionOrder", std::to_string(actual->executionOrder), ScenePrefabOverrideFlag::Behaviour);
    }
}

void AppendAudioSource(SceneComponents components, SceneEntity entity, const std::optional<AudioSourceComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const AudioSourceComponent* actual = components.AudioSources().TryGet(entity);
    if (EqualOptional(actual, expected, &Equal)) {
        return;
    }
    if (HasPresenceOverride(actual, expected, report, nodeIndex, object, "audioSource", ScenePrefabOverrideFlag::AudioSource)) {
        return;
    }
    if (!expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.clipAssetId", ScenePrefabOverrideValueFormatter::ToString(actual->clipAssetId), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.volume", ScenePrefabOverrideValueFormatter::ToString(actual->volume), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.pitch", ScenePrefabOverrideValueFormatter::ToString(actual->pitch), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.loop", ScenePrefabOverrideValueFormatter::ToString(actual->loop), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.spatial", ScenePrefabOverrideValueFormatter::ToString(actual->spatial), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.autoplay", ScenePrefabOverrideValueFormatter::ToString(actual->autoplay), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.enabled", ScenePrefabOverrideValueFormatter::ToString(actual->enabled), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.mute", ScenePrefabOverrideValueFormatter::ToString(actual->mute), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.pan", ScenePrefabOverrideValueFormatter::ToString(actual->pan), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.spatialBlend", ScenePrefabOverrideValueFormatter::ToString(actual->spatialBlend), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.attenuationModel", std::to_string(static_cast<int>(actual->attenuationModel)), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.minDistance", ScenePrefabOverrideValueFormatter::ToString(actual->minDistance), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.maxDistance", ScenePrefabOverrideValueFormatter::ToString(actual->maxDistance), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.rolloff", ScenePrefabOverrideValueFormatter::ToString(actual->rolloff), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.dopplerFactor", ScenePrefabOverrideValueFormatter::ToString(actual->dopplerFactor), ScenePrefabOverrideFlag::AudioSource);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.outputBus", std::string{ AudioSourceOutputBus(*actual) }, ScenePrefabOverrideFlag::AudioSource);
        return;
    }
    if (actual->clipAssetId != expected->clipAssetId) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.clipAssetId", ScenePrefabOverrideValueFormatter::ToString(actual->clipAssetId), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->volume != expected->volume) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.volume", ScenePrefabOverrideValueFormatter::ToString(actual->volume), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->pitch != expected->pitch) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.pitch", ScenePrefabOverrideValueFormatter::ToString(actual->pitch), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->loop != expected->loop) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.loop", ScenePrefabOverrideValueFormatter::ToString(actual->loop), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->spatial != expected->spatial) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.spatial", ScenePrefabOverrideValueFormatter::ToString(actual->spatial), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->autoplay != expected->autoplay) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.autoplay", ScenePrefabOverrideValueFormatter::ToString(actual->autoplay), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->enabled != expected->enabled) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.enabled", ScenePrefabOverrideValueFormatter::ToString(actual->enabled), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->mute != expected->mute) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.mute", ScenePrefabOverrideValueFormatter::ToString(actual->mute), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->pan != expected->pan) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.pan", ScenePrefabOverrideValueFormatter::ToString(actual->pan), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->spatialBlend != expected->spatialBlend) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.spatialBlend", ScenePrefabOverrideValueFormatter::ToString(actual->spatialBlend), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->attenuationModel != expected->attenuationModel) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.attenuationModel", std::to_string(static_cast<int>(actual->attenuationModel)), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->minDistance != expected->minDistance) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.minDistance", ScenePrefabOverrideValueFormatter::ToString(actual->minDistance), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->maxDistance != expected->maxDistance) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.maxDistance", ScenePrefabOverrideValueFormatter::ToString(actual->maxDistance), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->rolloff != expected->rolloff) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.rolloff", ScenePrefabOverrideValueFormatter::ToString(actual->rolloff), ScenePrefabOverrideFlag::AudioSource);
    }
    if (actual->dopplerFactor != expected->dopplerFactor) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.dopplerFactor", ScenePrefabOverrideValueFormatter::ToString(actual->dopplerFactor), ScenePrefabOverrideFlag::AudioSource);
    }
    if (AudioSourceOutputBus(*actual) != AudioSourceOutputBus(*expected)) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioSource.outputBus", std::string{ AudioSourceOutputBus(*actual) }, ScenePrefabOverrideFlag::AudioSource);
    }
}

void AppendAudioListener(SceneComponents components, SceneEntity entity, const std::optional<AudioListenerComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const AudioListenerComponent* actual = components.AudioListeners().TryGet(entity);
    if (EqualOptional(actual, expected, &Equal)) {
        return;
    }
    if (HasPresenceOverride(actual, expected, report, nodeIndex, object, "audioListener", ScenePrefabOverrideFlag::AudioListener)) {
        return;
    }
    if (!expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioListener.primary", ScenePrefabOverrideValueFormatter::ToString(actual->primary), ScenePrefabOverrideFlag::AudioListener);
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioListener.enabled", ScenePrefabOverrideValueFormatter::ToString(actual->enabled), ScenePrefabOverrideFlag::AudioListener);
        return;
    }
    if (actual->primary != expected->primary) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioListener.primary", ScenePrefabOverrideValueFormatter::ToString(actual->primary), ScenePrefabOverrideFlag::AudioListener);
    }
    if (actual->enabled != expected->enabled) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "audioListener.enabled", ScenePrefabOverrideValueFormatter::ToString(actual->enabled), ScenePrefabOverrideFlag::AudioListener);
    }
}

void AppendAnimator(SceneComponents components, SceneEntity entity, const std::optional<Animator>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const Animator* actual = components.Animators().TryGet(entity);
    const bool equal = actual == nullptr
        ? !expected.has_value()
        : expected.has_value() && actual->controllerAssetId == expected->controllerAssetId &&
            actual->speed == expected->speed && actual->enabled == expected->enabled &&
            actual->rootMotionOwner == expected->rootMotionOwner;
    if (equal) return;
    if (HasPresenceOverride(actual, expected, report, nodeIndex, object, "animator", ScenePrefabOverrideFlag::Animator)) return;
    if (!expected.has_value() || actual->controllerAssetId != expected->controllerAssetId) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "animator.controllerAssetId", ScenePrefabOverrideValueFormatter::ToString(actual->controllerAssetId), ScenePrefabOverrideFlag::Animator);
    }
    if (!expected.has_value() || actual->speed != expected->speed) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "animator.speed", ScenePrefabOverrideValueFormatter::ToString(actual->speed), ScenePrefabOverrideFlag::Animator);
    }
    if (!expected.has_value() || actual->enabled != expected->enabled) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "animator.enabled", ScenePrefabOverrideValueFormatter::ToString(actual->enabled), ScenePrefabOverrideFlag::Animator);
    }
    if (!expected.has_value() || actual->rootMotionOwner != expected->rootMotionOwner) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "animator.rootMotionOwner",
            ScenePrefabOverrideValueFormatter::ToString(static_cast<std::uint64_t>(actual->rootMotionOwner)), ScenePrefabOverrideFlag::Animator);
    }
}

void AppendUIDocument(SceneComponents components, SceneEntity entity, const std::optional<UIDocumentComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const UIDocumentComponent* actual = components.UIDocuments().TryGet(entity);
    const bool equal = actual == nullptr ? !expected.has_value() : expected.has_value() &&
        actual->documentAssetId == expected->documentAssetId && actual->enabled == expected->enabled;
    if (equal) return;
    if (HasPresenceOverride(actual, expected, report, nodeIndex, object, "uiDocument", ScenePrefabOverrideFlag::UIDocument)) return;
    if (!expected.has_value() || actual->documentAssetId != expected->documentAssetId) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "uiDocument.documentAssetId", ScenePrefabOverrideValueFormatter::ToString(actual->documentAssetId), ScenePrefabOverrideFlag::UIDocument);
    }
    if (!expected.has_value() || actual->enabled != expected->enabled) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "uiDocument.enabled", ScenePrefabOverrideValueFormatter::ToString(actual->enabled), ScenePrefabOverrideFlag::UIDocument);
    }
}

} // namespace

void ScenePrefabComponentOverrideReporter::Append(SceneComponents components, SceneEntity entity, const ScenePrefabNodeComponents& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    ScenePrefabCameraOverrideReporter::Append(components, entity, expected.camera, report, nodeIndex, object);
    ScenePrefabMeshRendererOverrideReporter::Append(components, entity, expected.meshRenderer, report, nodeIndex, object);
    ScenePrefabLightOverrideReporter::Append(components, entity, expected.light, report, nodeIndex, object);
    AppendInput(components, entity, expected.input, report, nodeIndex, object);
    AppendRigidbody(components, entity, expected.rigidbody, report, nodeIndex, object);
    AppendCollider(components, entity, expected.collider, report, nodeIndex, object);
    AppendTags(components, entity, expected.tags, report, nodeIndex, object);
    AppendBehaviour(components, entity, expected.behaviour, report, nodeIndex, object);
    AppendAudioSource(components, entity, expected.audioSource, report, nodeIndex, object);
    AppendAudioListener(components, entity, expected.audioListener, report, nodeIndex, object);
    AppendAnimator(components, entity, expected.animator, report, nodeIndex, object);
    AppendUIDocument(components, entity, expected.uiDocument, report, nodeIndex, object);
}

} // namespace kb::scene
