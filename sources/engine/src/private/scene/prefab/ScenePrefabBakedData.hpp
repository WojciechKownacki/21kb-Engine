#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kb::scene {

enum class ScenePrefabBakedComponentMask : std::uint16_t {
    Camera = 1U << 0U,
    MeshRenderer = 1U << 1U,
    Light = 1U << 2U,
    Input = 1U << 3U,
    Rigidbody = 1U << 4U,
    Collider = 1U << 5U,
    Tags = 1U << 6U,
    Behaviour = 1U << 7U,
    AudioSource = 1U << 8U,
    AudioListener = 1U << 9U,
    CharacterController = 1U << 10U,
    Joint = 1U << 11U,
    Animator = 1U << 12U,
};

[[nodiscard]] constexpr std::uint16_t ScenePrefabBakedMask(ScenePrefabBakedComponentMask mask) noexcept {
    return static_cast<std::uint16_t>(mask);
}

[[nodiscard]] constexpr bool ScenePrefabBakedMaskHas(std::uint16_t mask, ScenePrefabBakedComponentMask component) noexcept {
    return (mask & ScenePrefabBakedMask(component)) != 0U;
}

struct ScenePrefabBakedArchetype {
    std::uint16_t componentMask = 0U;
    std::vector<std::uint32_t> nodeIndices;
    std::vector<TransformComponent> transforms;
    std::vector<VisibilityComponent> visibility;
    std::vector<CameraComponent> cameras;
    std::vector<MeshRendererComponent> meshRenderers;
    std::vector<LightComponent> lights;
    std::vector<InputComponent> inputs;
    std::vector<RigidbodyComponent> rigidbodies;
    std::vector<ColliderComponent> colliders;
    std::vector<CharacterControllerComponent> characterControllers;
    std::vector<JointComponent> joints;
    std::vector<TagsComponent> tags;
    std::vector<BehaviourComponent> behaviours;
    std::vector<AudioSourceComponent> audioSources;
    std::vector<AudioListenerComponent> audioListeners;
    std::vector<Animator> animators;
};

class ScenePrefabBakedData {
public:
    [[nodiscard]] static ScenePrefabBakedData Bake(std::span<const ScenePrefabNodeDesc> nodes);

    [[nodiscard]] std::size_t NodeCount() const noexcept;
    [[nodiscard]] std::span<const ScenePrefabBakedArchetype> Archetypes() const noexcept;

private:
    std::size_t nodeCount_ = 0;
    std::vector<ScenePrefabBakedArchetype> archetypes_;
};

} // namespace kb::scene
