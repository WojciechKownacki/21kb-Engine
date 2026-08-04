#pragma once

#include "engine/core/ReadSnapshotQueue.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SkeletonAsset.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <algorithm>
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

struct AnimationBoneKeyframe {
    float timeSeconds = 0.0F;
    LocalTransform transform{};
};

struct AnimationBoneTrack {
    SkeletonBoneId boneId = 0U;
    std::uint64_t bindingMask = ~std::uint64_t{ 0U };
    std::vector<AnimationBoneKeyframe> keyframes;
};

struct AnimationMorphKeyframe {
    float timeSeconds = 0.0F;
    float weight = 0.0F;
};

struct AnimationMorphTrack {
    std::string morphTarget;
    std::vector<AnimationMorphKeyframe> keyframes;
};

struct AnimationCurveKeyframe {
    float timeSeconds = 0.0F;
    float value = 0.0F;
};

struct AnimationCurveTrack {
    std::string name;
    std::vector<AnimationCurveKeyframe> keyframes;
};

enum class AnimationRootMotionMode : std::uint8_t {
    None,
    ExtractFromBone,
};

struct AnimationClip {
    float durationSeconds = 1.0F;
    bool looping = true;
    std::vector<AnimationTransformTrack> tracks;
    std::vector<AnimationEventKeyframe> events;
    // A zero target preserves the existing generic Animator hierarchy clip.
    // A skeletal clip has exactly one canonical skeleton identity and uses
    // stable bone bindings rather than SceneEntity child-name paths.
    std::uint64_t targetSkeletonAssetId = 0U;
    std::uint64_t targetSkeletonCompatibilitySignature = 0U;
    std::vector<AnimationBoneTrack> skeletalTracks;
    std::vector<AnimationMorphTrack> morphTracks;
    std::vector<AnimationCurveTrack> curves;
    // ExtractFromBone makes the selected skeletal bone the sole source of the
    // component root-motion delta. None requires rootMotionBoneId to be zero.
    AnimationRootMotionMode rootMotionMode = AnimationRootMotionMode::None;
    SkeletonBoneId rootMotionBoneId = 0U;
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
    std::uint64_t id = 0U;
    std::string name;
    // Exactly one motion source is authored: a clipReference, or a 1D blend
    // tree driven by a Float parameter.
    std::string clipReference;
    std::string blendParameter;
    struct BlendChild {
        float threshold = 0.0F;
        std::string clipReference;
    };
    std::vector<BlendChild> blendChildren;
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
    std::uint64_t id = 0U;
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

enum class AnimatorRigConstraintType : std::uint8_t {
    TwoBoneIK,
    Aim,
    CopyTransform,
};

// Constraints are evaluated in authored order after all controller layers.
// A controller uses either SceneEntity paths or stable skeleton bone IDs for
// each constraint. Every driven path/bone may appear only once, making this
// list the sole rig authority after clip/blend-tree sampling.
struct AnimatorRigConstraint {
    std::string name;
    AnimatorRigConstraintType type = AnimatorRigConstraintType::TwoBoneIK;
    std::string constrainedPath;
    std::string midPath;
    std::string tipPath;
    // Named transient input. A constraint is intentionally inactive until
    // gameplay supplies this target through SceneAnimators/Animator script API.
    std::string target;
    std::string poleTarget;
    float weight = 1.0F;
    SkeletonBoneId constrainedBoneId = 0U;
    SkeletonBoneId midBoneId = 0U;
    SkeletonBoneId tipBoneId = 0U;
};

struct AnimatorGraphNodeLayout {
    std::uint64_t stateId = 0U;
    std::int32_t positionX = 0;
    std::int32_t positionY = 0;
};

struct AnimatorGraphComment {
    std::uint64_t id = 0U;
    std::string text;
    std::int32_t positionX = 0;
    std::int32_t positionY = 0;
    std::int32_t width = 240;
    std::int32_t height = 120;
};

struct AnimatorGraphGroup {
    std::uint64_t id = 0U;
    std::string name;
    std::vector<std::uint64_t> stateIds;
};

struct AnimatorController {
    std::vector<AnimatorParameterDefinition> parameters;
    std::vector<AnimatorControllerLayer> layers;
    std::vector<AnimatorRigConstraint> rigConstraints;
    std::vector<AnimatorGraphNodeLayout> graphLayout;
    std::vector<AnimatorGraphComment> graphComments;
    std::vector<AnimatorGraphGroup> graphGroups;
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

// Transient gameplay input consumed by constraints declared in the retained
// AnimatorController asset. This is runtime state, never serialized beside
// Animator or duplicated in the editor.
struct AnimatorIkTarget {
    Vec3 worldPosition{};
    Quat worldRotation{};
    float positionWeight = 1.0F;
    float rotationWeight = 1.0F;
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

// Immutable diagnostic state published only after an animator update has
// completed. It owns every exposed value and can be retained by editor or
// rendering threads without touching scene-owned mutable storage.
struct AnimatorDebugParameterSnapshot {
    std::string name;
    AnimatorParameterType type = AnimatorParameterType::Float;
    bool boolValue = false;
    std::int32_t intValue = 0;
    float floatValue = 0.0F;
};

struct AnimatorDebugLayerSnapshot {
    std::string name;
    std::uint64_t activeStateId = 0U;
    std::uint64_t previousStateId = 0U;
    std::uint64_t activeTransitionId = 0U;
    std::string activeState;
    std::string previousState;
    float normalizedTime = 0.0F;
    float transitionProgress = 1.0F;
    float weight = 1.0F;
    double elapsedSeconds = 0.0;
    bool transitioning = false;
};

struct AnimatorDebugBoneSnapshot {
    SkeletonBoneId id = 0U;
    LocalTransform localPose{};
    LocalTransform componentPose{};
};

struct AnimatorDebugConstraintSnapshot {
    std::string name;
    AnimatorRigConstraintType type = AnimatorRigConstraintType::TwoBoneIK;
    SkeletonBoneId constrainedBoneId = 0U;
    SkeletonBoneId midBoneId = 0U;
    SkeletonBoneId tipBoneId = 0U;
    std::string target;
    std::string poleTarget;
    AnimatorIkTarget targetValue{};
    AnimatorIkTarget poleTargetValue{};
    bool hasTarget = false;
    bool hasPoleTarget = false;
};

struct AnimatorDebugInstanceSnapshot {
    SceneEntity entity{};
    std::uint64_t controllerAssetId = 0U;
    std::uint64_t runtimeBindingGeneration = 0U;
    std::uint64_t skeletonAssetId = 0U;
    std::uint64_t skeletonCompatibilitySignature = 0U;
    std::uint64_t poseEvaluationCount = 0U;
    std::uint64_t hierarchySolveCount = 0U;
    std::uint32_t paletteMatrixCount = 0U;
    std::uint64_t skeletalMeshAssetId = 0U;
    std::int32_t lodBias = 0;
    bool lodEnabled = false;
    bool fixedBounds = false;
    bool deformedGeometryEnabled = false;
    Vec3 rootMotionTranslation{};
    Quat rootMotionRotation{};
    bool hasRootMotion = false;
    std::vector<Vec3> rootMotionTrail;
    std::vector<AnimatorDebugParameterSnapshot> parameters;
    std::vector<AnimatorDebugLayerSnapshot> layers;
    std::vector<AnimatorDebugBoneSnapshot> bones;
    std::vector<AnimatorDebugConstraintSnapshot> constraints;
    std::vector<std::string> compatibilityDiagnostics;
};

struct AnimatorDebugSnapshot final : kb::core::ReadSnapshot {
    std::vector<AnimatorDebugInstanceSnapshot> instances;

    [[nodiscard]] const AnimatorDebugInstanceSnapshot* Find(
        SceneEntity target) const noexcept {
        const auto it = std::find_if(
            instances.begin(), instances.end(),
            [target](const AnimatorDebugInstanceSnapshot& value) {
                return value.entity == target;
            });
        return it == instances.end() ? nullptr : &*it;
    }
};

} // namespace kb::scene
