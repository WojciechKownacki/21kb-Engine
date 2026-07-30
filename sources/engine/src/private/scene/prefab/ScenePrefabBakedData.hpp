#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kb::scene {

enum class ScenePrefabBakedComponentMask : std::uint32_t {
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
    UIDocument = 1U << 13U,
    NavAgent = 1U << 14U,
    NavObstacle = 1U << 15U,
    RegionShape = 1U << 16U,
    GuideCurve = 1U << 17U,
    ContentInstance = 1U << 18U,
};

[[nodiscard]] constexpr std::uint32_t ScenePrefabBakedMask(ScenePrefabBakedComponentMask mask) noexcept {
    return static_cast<std::uint32_t>(mask);
}

[[nodiscard]] constexpr bool ScenePrefabBakedMaskHas(std::uint32_t mask, ScenePrefabBakedComponentMask component) noexcept {
    return (mask & ScenePrefabBakedMask(component)) != 0U;
}

struct ScenePrefabBakedArchetype {
    std::uint32_t componentMask = 0U;
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
    std::vector<RegionShapeComponent> regionShapes;
    std::vector<GuideCurveComponent> guideCurves;
    std::vector<ContentInstanceComponent> contentInstances;
    std::vector<BehaviourComponent> behaviours;
    std::vector<AudioSourceComponent> audioSources;
    std::vector<AudioListenerComponent> audioListeners;
    std::vector<Animator> animators;
    std::vector<UIDocumentComponent> uiDocuments;
    std::vector<NavAgent> navAgents;
    std::vector<NavObstacle> navObstacles;
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
