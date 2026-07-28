#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/prefab/ScenePrefabBakedData.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace kb::scene {

struct ScenePrefabOptionalComponentMaskMatch {
    bool available = false;
    bool matches = false;
};

[[nodiscard]] inline std::uint16_t ScenePrefabOptionalComponentMask(const ScenePrefabNodeComponents& components) noexcept {
    std::uint16_t mask = 0U;
    if (components.camera.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Camera);
    }
    if (components.meshRenderer.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::MeshRenderer);
    }
    if (components.light.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Light);
    }
    if (components.input.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Input);
    }
    if (components.rigidbody.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Rigidbody);
    }
    if (components.collider.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Collider);
    }
    if (components.tags.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Tags);
    }
    if (components.behaviour.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Behaviour);
    }
    if (components.audioSource.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::AudioSource);
    }
    if (components.audioListener.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::AudioListener);
    }
    if (components.characterController.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::CharacterController);
    }
    if (components.joint.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Joint);
    }
    if (components.animator.has_value()) {
        mask |= ScenePrefabBakedMask(ScenePrefabBakedComponentMask::Animator);
    }
    return mask;
}

[[nodiscard]] inline std::array<kb::ecs::ComponentId, 13U> ScenePrefabOptionalComponentIds(const SceneComponentRegistry& registry) noexcept {
    return std::array<kb::ecs::ComponentId, 13U>{
        registry.CameraComponentId(),
        registry.MeshRendererComponentId(),
        registry.LightComponentId(),
        registry.InputComponentId(),
        registry.RigidbodyComponentId(),
        registry.ColliderComponentId(),
        registry.TagsComponentId(),
        registry.BehaviourComponentId(),
        registry.AudioSourceComponentId(),
        registry.AudioListenerComponentId(),
        registry.CharacterControllerComponentId(),
        registry.JointComponentId(),
        registry.AnimatorComponentId(),
    };
}

[[nodiscard]] inline ScenePrefabOptionalComponentMaskMatch ScenePrefabOptionalComponentMaskMatches(
    const SceneState& state,
    SceneEntity entity,
    const ScenePrefabNodeComponents& expected) noexcept {
    const kb::ecs::NativeArchetypeStorage& storage = state.world.NativeStorage();
    if (!storage.IsAlive(entity)) {
        return {};
    }

    const std::uint16_t expectedMask = ScenePrefabOptionalComponentMask(expected);
    const std::array<kb::ecs::ComponentId, 13U> componentIds = ScenePrefabOptionalComponentIds(state.components);
    std::array<kb::ecs::ComponentId, componentIds.size()> required{};
    std::array<kb::ecs::ComponentId, componentIds.size()> excluded{};
    std::size_t requiredCount = 0U;
    std::size_t excludedCount = 0U;
    for (std::size_t index = 0U; index < componentIds.size(); ++index) {
        const kb::ecs::ComponentId componentId = componentIds[index];
        if (componentId == 0U) {
            return {};
        }
        const std::uint16_t bit = static_cast<std::uint16_t>(1U << index);
        if ((expectedMask & bit) != 0U) {
            required[requiredCount++] = componentId;
        } else {
            excluded[excludedCount++] = componentId;
        }
    }

    return ScenePrefabOptionalComponentMaskMatch{
        .available = true,
        .matches = storage.EntityArchetypeMatches(
            entity,
            std::span<const kb::ecs::ComponentId>{ required.data(), requiredCount },
            std::span<const kb::ecs::ComponentId>{ excluded.data(), excludedCount }),
    };
}

} // namespace kb::scene
