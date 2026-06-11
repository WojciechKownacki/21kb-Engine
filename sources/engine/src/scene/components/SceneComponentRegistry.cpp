#include "scene/components/SceneComponentRegistry.hpp"

#include "engine/ecs/ComponentReflectionMacros.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

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
        }));
}

} // namespace

SceneComponentRegistry::SceneComponentRegistry(kb::ecs::World& world)
    : transformComponentId_(RegisterSceneComponent<TransformComponent>(world, "kb.scene.TransformComponent"))
    , visibilityComponentId_(RegisterSceneComponent<VisibilityComponent>(world, "kb.scene.VisibilityComponent"))
    , behaviourComponentId_(RegisterSceneComponent<BehaviourComponent>(world, "kb.scene.BehaviourComponent"))
    , cameraComponentId_(RegisterSceneComponent<CameraComponent>(world, "kb.scene.CameraComponent"))
    , meshRendererComponentId_(RegisterSceneComponent<MeshRendererComponent>(world, "kb.scene.MeshRendererComponent"))
    , lightComponentId_(RegisterSceneComponent<LightComponent>(world, "kb.scene.LightComponent"))
    , inputComponentId_(RegisterSceneComponent<InputComponent>(world, "kb.scene.InputComponent"))
    , rigidbodyComponentId_(RegisterSceneComponent<RigidbodyComponent>(world, "kb.scene.RigidbodyComponent"))
    , colliderComponentId_(RegisterSceneComponent<ColliderComponent>(world, "kb.scene.ColliderComponent")) {
    RegisterPhysicsReflection(world);
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

} // namespace kb::scene
