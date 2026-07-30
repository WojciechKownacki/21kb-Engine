#pragma once

#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Navigation.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/GuideCurveComponent.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "engine/scene/TagsComponent.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/scene/UIAssets.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kb::scene {

// Persistent joint data deliberately stores a prefab-node stable id instead of
// SceneEntity. SceneEntity is a live ECS handle and is only resolved after an
// instance has created all of its nodes.
struct ScenePrefabJointComponent {
    static constexpr std::uint64_t InvalidConnectedNodeStableId = 0U;
    // Capture uses this only to make an external live-entity reference
    // unambiguously invalid. Validator rejects it before a prefab can be
    // saved or instantiated; it can never degrade into a world joint.
    static constexpr std::uint64_t UnresolvedConnectedNodeStableId = UINT64_MAX;

    JointType type = JointType::Fixed;
    std::uint64_t connectedNodeStableId = InvalidConnectedNodeStableId;
    Vec3 anchor{};
    Vec3 connectedAnchor{};
    Vec3 axis{ 0.0F, 1.0F, 0.0F };
    float minLimit = 0.0F;
    float maxLimit = 0.0F;
    bool enableLimit = false;
};

struct ScenePrefabNodeComponents {
    std::optional<CameraComponent> camera;
    std::optional<MeshRendererComponent> meshRenderer;
    std::optional<LightComponent> light;
    std::optional<InputComponent> input;
    std::optional<RigidbodyComponent> rigidbody;
    std::optional<ColliderComponent> collider;
    std::optional<CharacterControllerComponent> characterController;
    std::optional<ScenePrefabJointComponent> joint;
    std::optional<TagsComponent> tags;
    std::optional<RegionShapeComponent> regionShape;
    std::optional<GuideCurveComponent> guideCurve;
    std::optional<ContentInstanceComponent> contentInstance;
    std::optional<StreamFocusComponent> streamFocus;
    std::optional<BehaviourComponent> behaviour;
    std::optional<AudioSourceComponent> audioSource;
    std::optional<AudioListenerComponent> audioListener;
    std::optional<Animator> animator;
    std::optional<UIDocumentComponent> uiDocument;
    std::optional<NavAgent> navAgent;
    std::optional<NavObstacle> navObstacle;
};

struct ScenePrefabNodeDesc {
    static constexpr std::uint32_t NoParent = UINT32_MAX;
    static constexpr std::uint64_t InvalidStableId = 0;

    std::uint64_t stableId = InvalidStableId;
    std::string name;
    std::string nestedPrefabGuid;
    std::vector<ScenePrefabPropertyOverride> nestedPrefabOverrides;
    std::uint32_t parentNode = NoParent;
    TransformComponent transform{};
    VisibilityComponent visibility{};
    ScenePrefabNodeComponents components{};
};

} // namespace kb::scene
