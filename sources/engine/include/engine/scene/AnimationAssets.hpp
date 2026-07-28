#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

struct AnimationTransformKeyframe {
    float timeSeconds = 0.0F;
    LocalTransform transform{};
};

struct AnimationTransformTrack {
    // Empty or "." addresses the Animator owner. Otherwise this is a
    // slash-separated child-name path relative to the owner.
    std::string targetPath;
    // Authored binding bits. A controller layer evaluates this track only
    // when at least one bit intersects the layer mask.
    std::uint64_t bindingMask = ~std::uint64_t{ 0U };
    std::vector<AnimationTransformKeyframe> keyframes;
};

using AnimationEventId = std::uint64_t;

// A typed marker carried by an AnimationClip. The id is authored data, not
// a function/method name: runtime dispatch always uses the fixed,
// versioned OnAnimationEvent schema.
struct AnimationEventKeyframe {
    float timeSeconds = 0.0F;
    AnimationEventId id = 0U;
};

struct AnimationClip {
    float durationSeconds = 1.0F;
    bool looping = true;
    std::vector<AnimationTransformTrack> tracks;
    std::vector<AnimationEventKeyframe> events;
};

enum class AnimatorParameterType : std::uint8_t {
    Bool,
    Int,
    Float,
    Trigger,
};

struct AnimatorParameterDefinition {
    std::string name;
    AnimatorParameterType type = AnimatorParameterType::Float;
    bool boolDefault = false;
    std::int32_t intDefault = 0;
    float floatDefault = 0.0F;
};

struct AnimatorControllerState {
    std::string name;
    std::string clipReference;
};

enum class AnimatorConditionMode : std::uint8_t {
    BoolEquals,
    IntEquals,
    IntGreater,
    IntLess,
    FloatGreater,
    FloatLess,
    TriggerSet,
};

struct AnimatorTransitionCondition {
    std::string parameter;
    AnimatorConditionMode mode = AnimatorConditionMode::BoolEquals;
    bool boolValue = false;
    std::int32_t intValue = 0;
    float floatValue = 0.0F;
};

struct AnimatorControllerTransition {
    std::string fromState;
    std::string toState;
    float durationSeconds = 0.1F;
    // Negative disables exit-time gating. Otherwise the transition becomes
    // eligible after the source state reaches this normalized time.
    float exitNormalizedTime = -1.0F;
    std::vector<AnimatorTransitionCondition> conditions;
};

struct AnimatorControllerLayer {
    std::string name;
    std::string defaultState;
    float weight = 1.0F;
    std::uint64_t mask = ~std::uint64_t{ 0U };
    std::vector<AnimatorControllerState> states;
    std::vector<AnimatorControllerTransition> transitions;
};

struct AnimatorController {
    std::vector<AnimatorParameterDefinition> parameters;
    std::vector<AnimatorControllerLayer> layers;
};

enum class AnimatorRootMotionOwner : std::uint8_t {
    // Root track is evaluated as an ordinary authored local transform.
    None,
    // Animator extracts the first layer's owner track and accumulates its
    // local translation/rotation directly on the owner Transform.
    Animator,
    // World-planar delta and upright/yaw rotation are routed through an
    // exclusive CharacterController; gravity and jump retain exclusive
    // ownership of vertical movement.
    CharacterController,
    // Delta is routed through an exclusive kinematic Rigidbody + Collider.
    // Dynamic/static bodies and a co-located CharacterController are rejected
    // because they would create a second transform authority.
    Rigidbody,
};

// The single authored configuration component. Scene/prefab serialization and
// the editor mutate this value; the animation system derives non-serialized
// playheads, parameters, blends and retained asset handles from it.
struct Animator {
    std::uint64_t controllerAssetId = 0U;
    float speed = 1.0F;
    bool enabled = true;
    AnimatorRootMotionOwner rootMotionOwner = AnimatorRootMotionOwner::None;
};

struct AnimatorParameterValue {
    AnimatorParameterType type = AnimatorParameterType::Float;
    bool boolValue = false;
    std::int32_t intValue = 0;
    float floatValue = 0.0F;
};

struct AnimatorStateInfo {
    // Own the names so a query result remains valid after Detach(), entity
    // destruction, or replacing the attached controller.
    std::string state;
    std::string previousState;
    float normalizedTime = 0.0F;
    float transitionProgress = 1.0F;
    bool transitioning = false;
};

struct AnimationEventRecord {
    static constexpr std::int32_t kSchemaMajor = 1;
    static constexpr std::int32_t kSchemaMinor = 0;

    SceneEntity target{};
    std::int32_t schemaMajor = kSchemaMajor;
    std::int32_t schemaMinor = kSchemaMinor;
    AnimationEventId eventId = 0U;
    std::uint64_t clipAssetId = 0U;
    std::string layer;
    std::string state;
    float normalizedTime = 0.0F;
};

} // namespace kb::scene
