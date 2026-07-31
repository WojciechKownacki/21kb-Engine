#include "scene/components/SceneComponentRegistry.hpp"

#include "engine/ecs/ComponentReflectionMacros.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/TagsComponent.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/GuideCurveComponent.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/scene/UIAssets.hpp"

#include <string_view>

namespace kb::scene {
namespace {

template <typename T>
[[nodiscard]] std::uint64_t RegisterSceneComponent(kb::ecs::World& world, std::string_view name) {
    return world.RegisterComponent<T>(name);
}

void RegisterPhysicsReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<RigidbodyComponent>(
        "kb.scene.RigidbodyComponent",
        {
            KB_ECS_FIELD(RigidbodyComponent, bodyType, kb::ecs::ComponentFieldType::Enum32),
            KB_ECS_FIELD(RigidbodyComponent, mass, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(RigidbodyComponent, linearVelocity, kb::ecs::ComponentFieldType::Vec3Float32),
            KB_ECS_FIELD(RigidbodyComponent, angularVelocity, kb::ecs::ComponentFieldType::Vec3Float32),
            KB_ECS_FIELD(RigidbodyComponent, gravityScale, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(RigidbodyComponent, useGravity, kb::ecs::ComponentFieldType::Bool),
            KB_ECS_FIELD(RigidbodyComponent, lockRotation, kb::ecs::ComponentFieldType::Bool),
        }));
    static_cast<void>(world.RegisterComponentReflection<ColliderComponent>(
        "kb.scene.ColliderComponent",
        {
            KB_ECS_FIELD(ColliderComponent, shape, kb::ecs::ComponentFieldType::Enum32),
            KB_ECS_FIELD(ColliderComponent, center, kb::ecs::ComponentFieldType::Vec3Float32),
            KB_ECS_FIELD(ColliderComponent, boxSize, kb::ecs::ComponentFieldType::Vec3Float32),
            KB_ECS_FIELD(ColliderComponent, radius, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(ColliderComponent, height, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(ColliderComponent, trigger, kb::ecs::ComponentFieldType::Bool),
            KB_ECS_FIELD(ColliderComponent, friction, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(ColliderComponent, restitution, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(ColliderComponent, layer, kb::ecs::ComponentFieldType::UInt32),
        }));
}

void RegisterAudioReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<AudioListenerComponent>(
        "kb.scene.AudioListenerComponent",
        {
            KB_ECS_FIELD(AudioListenerComponent, primary, kb::ecs::ComponentFieldType::Bool),
            KB_ECS_FIELD(AudioListenerComponent, enabled, kb::ecs::ComponentFieldType::Bool),
        }));
    static_cast<void>(world.RegisterComponentReflection<AudioSourceComponent>(
        "kb.scene.AudioSourceComponent",
        {
            KB_ECS_FIELD(AudioSourceComponent, clipAssetId, kb::ecs::ComponentFieldType::Bytes),
            KB_ECS_FIELD(AudioSourceComponent, volume, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(AudioSourceComponent, pitch, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(AudioSourceComponent, loop, kb::ecs::ComponentFieldType::Bool),
            KB_ECS_FIELD(AudioSourceComponent, spatial, kb::ecs::ComponentFieldType::Bool),
            KB_ECS_FIELD(AudioSourceComponent, autoplay, kb::ecs::ComponentFieldType::Bool),
            KB_ECS_FIELD(AudioSourceComponent, enabled, kb::ecs::ComponentFieldType::Bool),
            KB_ECS_FIELD(AudioSourceComponent, mute, kb::ecs::ComponentFieldType::Bool),
            KB_ECS_FIELD(AudioSourceComponent, pan, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(AudioSourceComponent, spatialBlend, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(AudioSourceComponent, attenuationModel, kb::ecs::ComponentFieldType::Enum32),
            KB_ECS_FIELD(AudioSourceComponent, minDistance, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(AudioSourceComponent, maxDistance, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(AudioSourceComponent, rolloff, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(AudioSourceComponent, dopplerFactor, kb::ecs::ComponentFieldType::Float32),
        }));
}

void RegisterVisibilityBlockerReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<SceneVisibilityBlockerComponent>(
        SceneVisibilityBlockerComponent::StableId,
        {
            KB_ECS_FIELD(SceneVisibilityBlockerComponent, localCenter, kb::ecs::ComponentFieldType::Vec3Float32),
            KB_ECS_FIELD(SceneVisibilityBlockerComponent, size, kb::ecs::ComponentFieldType::Vec3Float32),
            KB_ECS_FIELD(SceneVisibilityBlockerComponent, enabled, kb::ecs::ComponentFieldType::Bool),
        }));
}

void RegisterRegionShapeReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<RegionShapeComponent>(
        "kb.scene.RegionShapeComponent",
        {
            KB_ECS_FIELD(RegionShapeComponent, kind, kb::ecs::ComponentFieldType::Enum32),
            KB_ECS_FIELD(RegionShapeComponent, center, kb::ecs::ComponentFieldType::Vec3Float32),
            KB_ECS_FIELD(RegionShapeComponent, size, kb::ecs::ComponentFieldType::Vec3Float32),
            KB_ECS_FIELD(RegionShapeComponent, radius, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(RegionShapeComponent, height, kb::ecs::ComponentFieldType::Float32),
            KB_ECS_FIELD(RegionShapeComponent, enabled, kb::ecs::ComponentFieldType::Bool),
        }));
}

void RegisterGuideCurveReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<GuideCurveComponent>(
        "kb.scene.GuideCurveComponent",
        {
            KB_ECS_FIELD(GuideCurveComponent, controlPointCount, kb::ecs::ComponentFieldType::UInt32),
            KB_ECS_FIELD(GuideCurveComponent, interpolation, kb::ecs::ComponentFieldType::Enum32),
            KB_ECS_FIELD(GuideCurveComponent, closed, kb::ecs::ComponentFieldType::Bool),
            KB_ECS_FIELD(GuideCurveComponent, enabled, kb::ecs::ComponentFieldType::Bool),
        }));
}

void RegisterContentInstanceReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<ContentInstanceComponent>("kb.scene.ContentInstanceComponent", {
        KB_ECS_FIELD(ContentInstanceComponent, assetId, kb::ecs::ComponentFieldType::Bytes),
        KB_ECS_FIELD(ContentInstanceComponent, kind, kb::ecs::ComponentFieldType::Enum32),
        KB_ECS_FIELD(ContentInstanceComponent, lifetime, kb::ecs::ComponentFieldType::Enum32),
        KB_ECS_FIELD(ContentInstanceComponent, active, kb::ecs::ComponentFieldType::Bool),
    }));
}

void RegisterStreamFocusReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<StreamFocusComponent>("kb.scene.StreamFocusComponent", {
        KB_ECS_FIELD(StreamFocusComponent, innerRadius, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(StreamFocusComponent, outerRadius, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(StreamFocusComponent, priority, kb::ecs::ComponentFieldType::Int32),
        KB_ECS_FIELD(StreamFocusComponent, loadMask, kb::ecs::ComponentFieldType::UInt32),
        KB_ECS_FIELD(StreamFocusComponent, enabled, kb::ecs::ComponentFieldType::Bool),
    }));
}

void RegisterWorldBackdropReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<WorldBackdropComponent>(WorldBackdropComponent::StableId, {
        KB_ECS_FIELD(WorldBackdropComponent, mode, kb::ecs::ComponentFieldType::Enum32),
        KB_ECS_FIELD(WorldBackdropComponent, color, kb::ecs::ComponentFieldType::Vec3Float32),
        KB_ECS_FIELD(WorldBackdropComponent, horizonColor, kb::ecs::ComponentFieldType::Vec3Float32),
        KB_ECS_FIELD(WorldBackdropComponent, zenithColor, kb::ecs::ComponentFieldType::Vec3Float32),
        KB_ECS_FIELD(WorldBackdropComponent, environmentAssetId, kb::ecs::ComponentFieldType::Bytes),
        KB_ECS_FIELD(WorldBackdropComponent, horizonHeight, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(WorldBackdropComponent, gradientExponent, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(WorldBackdropComponent, priority, kb::ecs::ComponentFieldType::Int32),
        KB_ECS_FIELD(WorldBackdropComponent, enabled, kb::ecs::ComponentFieldType::Bool),
    }));
}

void RegisterAmbientRadianceReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<AmbientRadianceComponent>(AmbientRadianceComponent::StableId, {
        KB_ECS_FIELD(AmbientRadianceComponent, mode, kb::ecs::ComponentFieldType::Enum32),
        KB_ECS_FIELD(AmbientRadianceComponent, color, kb::ecs::ComponentFieldType::Vec3Float32),
        KB_ECS_FIELD(AmbientRadianceComponent, horizonColor, kb::ecs::ComponentFieldType::Vec3Float32),
        KB_ECS_FIELD(AmbientRadianceComponent, zenithColor, kb::ecs::ComponentFieldType::Vec3Float32),
        KB_ECS_FIELD(AmbientRadianceComponent, environmentAssetId, kb::ecs::ComponentFieldType::Bytes),
        KB_ECS_FIELD(AmbientRadianceComponent, intensity, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(AmbientRadianceComponent, diffuseIntensity, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(AmbientRadianceComponent, specularIntensity, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(AmbientRadianceComponent, priority, kb::ecs::ComponentFieldType::Int32),
        KB_ECS_FIELD(AmbientRadianceComponent, enabled, kb::ecs::ComponentFieldType::Bool),
    }));
}

void RegisterDetailSwitchReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<SceneDetailSwitchComponent>(SceneDetailSwitchComponent::StableId, {
        KB_ECS_FIELD(SceneDetailSwitchComponent, groupId, kb::ecs::ComponentFieldType::Bytes),
        KB_ECS_FIELD(SceneDetailSwitchComponent, minimumLod, kb::ecs::ComponentFieldType::UInt32),
        KB_ECS_FIELD(SceneDetailSwitchComponent, maximumLod, kb::ecs::ComponentFieldType::UInt32),
        KB_ECS_FIELD(SceneDetailSwitchComponent, promoteCoverage, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(SceneDetailSwitchComponent, demoteCoverage, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(SceneDetailSwitchComponent, enabled, kb::ecs::ComponentFieldType::Bool),
    }));
}

void RegisterLightReflection(kb::ecs::World& world) {
    static_cast<void>(world.RegisterComponentReflection<LightComponent>(LightComponent::StableId, {
        KB_ECS_FIELD(LightComponent, kind, kb::ecs::ComponentFieldType::Enum32),
        KB_ECS_FIELD(LightComponent, color, kb::ecs::ComponentFieldType::Vec3Float32),
        KB_ECS_FIELD(LightComponent, intensity, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(LightComponent, range, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(LightComponent, innerConeDegrees, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(LightComponent, outerConeDegrees, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(LightComponent, areaWidth, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(LightComponent, areaHeight, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(LightComponent, contactShadowLength, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(LightComponent, volumetricScattering, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(LightComponent, castsShadow, kb::ecs::ComponentFieldType::Bool),
        KB_ECS_FIELD(LightComponent, useColorTemperature, kb::ecs::ComponentFieldType::Bool),
        KB_ECS_FIELD(LightComponent, colorTemperatureKelvin, kb::ecs::ComponentFieldType::Float32),
        KB_ECS_FIELD(LightComponent, layerMask, kb::ecs::ComponentFieldType::UInt32),
    }));
}

} // namespace

SceneComponentRegistry::SceneComponentRegistry(kb::ecs::World& world)
    : transformComponentId_(RegisterSceneComponent<TransformComponent>(world, "kb.scene.TransformComponent"))
    , visibilityComponentId_(RegisterSceneComponent<VisibilityComponent>(world, VisibilityComponent::StableId))
    , behaviourComponentId_(RegisterSceneComponent<BehaviourComponent>(world, "kb.scene.BehaviourComponent"))
    , cameraComponentId_(RegisterSceneComponent<CameraComponent>(world, CameraComponent::StableId))
    , meshRendererComponentId_(RegisterSceneComponent<MeshRendererComponent>(world, MeshRendererComponent::StableId))
    , lightComponentId_(RegisterSceneComponent<LightComponent>(world, LightComponent::StableId))
    , inputComponentId_(RegisterSceneComponent<InputComponent>(world, "kb.scene.InputComponent"))
    , rigidbodyComponentId_(RegisterSceneComponent<RigidbodyComponent>(world, "kb.scene.RigidbodyComponent"))
    , colliderComponentId_(RegisterSceneComponent<ColliderComponent>(world, "kb.scene.ColliderComponent"))
    , characterControllerComponentId_(RegisterSceneComponent<CharacterControllerComponent>(world, "kb.scene.CharacterControllerComponent"))
    , jointComponentId_(RegisterSceneComponent<JointComponent>(world, "kb.scene.JointComponent"))
    , tagsComponentId_(RegisterSceneComponent<TagsComponent>(world, TagsComponent::StableId))
    , regionShapeComponentId_(RegisterSceneComponent<RegionShapeComponent>(world, RegionShapeComponent::StableId))
    , guideCurveComponentId_(RegisterSceneComponent<GuideCurveComponent>(world, GuideCurveComponent::StableId))
    , contentInstanceComponentId_(RegisterSceneComponent<ContentInstanceComponent>(world, ContentInstanceComponent::StableId))
    , streamFocusComponentId_(RegisterSceneComponent<StreamFocusComponent>(world, StreamFocusComponent::StableId))
    , worldBackdropComponentId_(RegisterSceneComponent<WorldBackdropComponent>(world, WorldBackdropComponent::StableId))
    , ambientRadianceComponentId_(RegisterSceneComponent<AmbientRadianceComponent>(world, AmbientRadianceComponent::StableId))
    , detailSwitchComponentId_(RegisterSceneComponent<SceneDetailSwitchComponent>(world, SceneDetailSwitchComponent::StableId))
    , visibilityBlockerComponentId_(RegisterSceneComponent<SceneVisibilityBlockerComponent>(world, SceneVisibilityBlockerComponent::StableId))
    , audioSourceComponentId_(RegisterSceneComponent<AudioSourceComponent>(world, "kb.scene.AudioSourceComponent"))
    , audioListenerComponentId_(RegisterSceneComponent<AudioListenerComponent>(world, "kb.scene.AudioListenerComponent"))
    , animatorComponentId_(RegisterSceneComponent<Animator>(world, "kb.scene.AnimatorComponent"))
    , uiDocumentComponentId_(RegisterSceneComponent<UIDocumentComponent>(world, "kb.scene.UIDocumentComponent"))
    , navAgentComponentId_(RegisterSceneComponent<NavAgent>(world, "kb.scene.NavAgent"))
    , navObstacleComponentId_(RegisterSceneComponent<NavObstacle>(world, "kb.scene.NavObstacle")) {
    RegisterPhysicsReflection(world);
    RegisterAudioReflection(world);
    RegisterLightReflection(world);
    RegisterRegionShapeReflection(world);
    RegisterGuideCurveReflection(world);
    RegisterContentInstanceReflection(world);
    RegisterStreamFocusReflection(world);
    RegisterWorldBackdropReflection(world);
    RegisterAmbientRadianceReflection(world);
    RegisterDetailSwitchReflection(world);
    RegisterVisibilityBlockerReflection(world);
}

std::uint64_t SceneComponentRegistry::TransformComponentId() const noexcept {
    return transformComponentId_;
}

std::uint64_t SceneComponentRegistry::VisibilityComponentId() const noexcept {
    return visibilityComponentId_;
}

std::uint64_t SceneComponentRegistry::BehaviourComponentId() const noexcept {
    return behaviourComponentId_;
}

std::uint64_t SceneComponentRegistry::CameraComponentId() const noexcept {
    return cameraComponentId_;
}

std::uint64_t SceneComponentRegistry::MeshRendererComponentId() const noexcept {
    return meshRendererComponentId_;
}

std::uint64_t SceneComponentRegistry::LightComponentId() const noexcept {
    return lightComponentId_;
}

std::uint64_t SceneComponentRegistry::InputComponentId() const noexcept {
    return inputComponentId_;
}

std::uint64_t SceneComponentRegistry::RigidbodyComponentId() const noexcept {
    return rigidbodyComponentId_;
}

std::uint64_t SceneComponentRegistry::ColliderComponentId() const noexcept {
    return colliderComponentId_;
}

std::uint64_t SceneComponentRegistry::CharacterControllerComponentId() const noexcept {
    return characterControllerComponentId_;
}

std::uint64_t SceneComponentRegistry::JointComponentId() const noexcept {
    return jointComponentId_;
}

std::uint64_t SceneComponentRegistry::TagsComponentId() const noexcept {
    return tagsComponentId_;
}

std::uint64_t SceneComponentRegistry::RegionShapeComponentId() const noexcept {
    return regionShapeComponentId_;
}
std::uint64_t SceneComponentRegistry::GuideCurveComponentId() const noexcept { return guideCurveComponentId_; }
std::uint64_t SceneComponentRegistry::ContentInstanceComponentId() const noexcept { return contentInstanceComponentId_; }
std::uint64_t SceneComponentRegistry::StreamFocusComponentId() const noexcept { return streamFocusComponentId_; }
std::uint64_t SceneComponentRegistry::WorldBackdropComponentId() const noexcept { return worldBackdropComponentId_; }
std::uint64_t SceneComponentRegistry::AmbientRadianceComponentId() const noexcept { return ambientRadianceComponentId_; }
std::uint64_t SceneComponentRegistry::DetailSwitchComponentId() const noexcept { return detailSwitchComponentId_; }
std::uint64_t SceneComponentRegistry::VisibilityBlockerComponentId() const noexcept { return visibilityBlockerComponentId_; }

std::uint64_t SceneComponentRegistry::AudioSourceComponentId() const noexcept {
    return audioSourceComponentId_;
}

std::uint64_t SceneComponentRegistry::AudioListenerComponentId() const noexcept {
    return audioListenerComponentId_;
}

std::uint64_t SceneComponentRegistry::AnimatorComponentId() const noexcept { return animatorComponentId_; }
std::uint64_t SceneComponentRegistry::UIDocumentComponentId() const noexcept { return uiDocumentComponentId_; }
std::uint64_t SceneComponentRegistry::NavAgentComponentId() const noexcept { return navAgentComponentId_; }
std::uint64_t SceneComponentRegistry::NavObstacleComponentId() const noexcept { return navObstacleComponentId_; }

} // namespace kb::scene
