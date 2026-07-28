#include "JoltPhysicsSceneSystem.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/World.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/PhysicsLayersAsset.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneSystemTransformAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Math/Math.h>
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/TransformedShape.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

JPH_SUPPRESS_WARNINGS

namespace kb::physics_jolt {

using kb::scene::CharacterControllerComponent;
using kb::scene::ColliderComponent;
using kb::scene::ColliderShape;
using kb::scene::JointComponent;
using kb::scene::JointType;
using kb::scene::Quat;
using kb::scene::RigidbodyBodyType;
using kb::scene::RigidbodyComponent;
using kb::scene::SceneEntity;
using kb::scene::SceneSystemContext;
using kb::scene::TransformComponent;
using kb::scene::Vec3;

namespace {

constexpr float MinimumShapeExtent = 0.001F;
constexpr std::uint32_t MaxBodies = 65536U;
constexpr std::uint32_t NumBodyMutexes = 0U;
constexpr std::uint32_t MaxBodyPairs = 65536U;
constexpr std::uint32_t MaxContactConstraints = 10240U;
constexpr std::size_t MaxQueuedRootMotionSegments = 256U;
// LIB-129: a named layer (ColliderComponent::layer's lowest set bit, 0-31 -
// see kb::scene::PhysicsLayersAsset) combined with static/dynamic motion
// type into a single Jolt ObjectLayer - see ToObjectLayer below for why
// static/dynamic still needs its own axis even though it is now orthogonal
// to the named layer.
constexpr JPH::uint kNamedLayerCount = kb::scene::kPhysicsLayerCount;
constexpr JPH::uint kObjectLayerCount = kNamedLayerCount * 2U;

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NonMoving(0);
constexpr JPH::BroadPhaseLayer Moving(1);
constexpr JPH::uint Count = 2;
} // namespace BroadPhaseLayers

// Two bodies on the SAME named layer but different motion types still need
// DIFFERENT object layers, because Jolt's BroadPhaseLayerInterface maps
// object layer -> broadphase layer through a fixed, per-object-layer table
// (no per-body override) and broadphase layer must still distinguish static
// from dynamic for Jolt's own broad-phase pruning to work correctly -
// doubling the object layer count is the standard way to combine this
// orthogonal "which broadphase bucket" axis with the "which named layer"
// axis in Jolt's object-layer model.
[[nodiscard]] constexpr JPH::ObjectLayer ToObjectLayer(std::uint32_t namedLayer, bool isStatic) noexcept {
    return static_cast<JPH::ObjectLayer>(namedLayer * 2U + (isStatic ? 0U : 1U));
}

// Computes ShouldCollide LIVE from the mutable, live-reconfigurable-via-
// IPhysicsBackend::ConfigureLayers JPH::ObjectLayerPairFilterTable and the
// fixed object-layer -> broadphase-layer mapping, instead of using Jolt's
// own ObjectVsBroadPhaseLayerFilterTable, which snapshots ShouldCollide
// results once at CONSTRUCTION time: a snapshot taken before ConfigureLayers
// is ever called would stay stale after a later ConfigureLayers call, with
// Jolt's broad-phase silently continuing to cull pairs that should now
// collide.
class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    ObjectVsBroadPhaseLayerFilter(const JPH::ObjectLayerPairFilter& pairFilter, const JPH::BroadPhaseLayerInterface& broadPhaseLayers) noexcept
        : pairFilter_(pairFilter)
        , broadPhaseLayers_(broadPhaseLayers) {}

    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer) const override {
        for (JPH::ObjectLayer other = 0; other < static_cast<JPH::ObjectLayer>(kObjectLayerCount); ++other) {
            if (broadPhaseLayers_.GetBroadPhaseLayer(other) == broadPhaseLayer && pairFilter_.ShouldCollide(objectLayer, other)) {
                return true;
            }
        }
        return false;
    }

private:
    const JPH::ObjectLayerPairFilter& pairFilter_;
    const JPH::BroadPhaseLayerInterface& broadPhaseLayers_;
};

class JoltRuntime {
public:
    JoltRuntime() {
        std::lock_guard lock{ mutex_ };
        if (instanceCount_ == 0U && !registered_) {
            JPH::RegisterDefaultAllocator();
            if (JPH::Factory::sInstance == nullptr) {
                JPH::Factory::sInstance = new JPH::Factory();
            }
            JPH::RegisterTypes();
            registered_ = true;
        }
        ++instanceCount_;
    }

    ~JoltRuntime() {
        std::lock_guard lock{ mutex_ };
        if (instanceCount_ == 0U) {
            return;
        }
        --instanceCount_;
        // Keep the Jolt registry alive for the process lifetime. Unregistering
        // during plugin teardown is order-sensitive on Windows and can fault
        // after all bodies have already been removed cleanly.
    }

    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;

private:
    inline static std::mutex mutex_;
    inline static std::size_t instanceCount_ = 0U;
    inline static bool registered_ = false;
};

struct BodySignature {
    RigidbodyBodyType bodyType = RigidbodyBodyType::Dynamic;
    ColliderShape shape = ColliderShape::Box;
    Vec3 scale{ 1.0F, 1.0F, 1.0F };
    Vec3 center{};
    Vec3 boxSize{ 1.0F, 1.0F, 1.0F };
    float radius = 0.5F;
    float height = 2.0F;
    float mass = 1.0F;
    float gravityScale = 1.0F;
    bool useGravity = true;
    bool lockRotation = false;
    bool useContinuousCollision = false;
    bool trigger = false;
    float friction = 0.5F;
    float restitution = 0.0F;
    std::uint32_t layer = 0x7FFFFFFFU;
};

struct BodyRecord {
    JPH::BodyID bodyId{};
    BodySignature signature{};
    // MoveKinematic writes a velocity directly into the live Jolt body. The
    // next fixed-step ECS synchronization must not immediately replace that
    // target with the previous Transform pose before Jolt gets one chance to
    // integrate it. Cleared by SynchronizeBody after exactly one skipped
    // transform-driven synchronization; WriteBack then publishes the new pose
    // as the next authoritative Transform.
    bool pendingKinematicMove = false;
};

[[nodiscard]] float ClampPositive(float value) noexcept {
    return std::max(value, MinimumShapeExtent);
}

[[nodiscard]] bool IsPositiveFinite(float value) noexcept {
    return std::isfinite(value) && value >= MinimumShapeExtent;
}

[[nodiscard]] float AbsScale(float value) noexcept {
    return std::max(std::fabs(value), MinimumShapeExtent);
}

[[nodiscard]] JPH::Vec3 ToJolt(Vec3 value) noexcept {
    return JPH::Vec3(value.x, value.y, value.z);
}

[[nodiscard]] JPH::RVec3 ToJoltPosition(Vec3 value) noexcept {
    return JPH::RVec3(value.x, value.y, value.z);
}

[[nodiscard]] JPH::Quat ToJolt(Quat value) noexcept {
    return JPH::Quat(value.x, value.y, value.z, value.w);
}

[[nodiscard]] Vec3 FromJolt(JPH::Vec3 value) noexcept {
    return Vec3{ value.GetX(), value.GetY(), value.GetZ() };
}

[[nodiscard]] Vec3 FromJoltPosition(JPH::RVec3 value) noexcept {
    return Vec3{ static_cast<float>(value.GetX()), static_cast<float>(value.GetY()), static_cast<float>(value.GetZ()) };
}

[[nodiscard]] Quat FromJolt(JPH::Quat value) noexcept {
    return Quat{ value.GetX(), value.GetY(), value.GetZ(), value.GetW() };
}

[[nodiscard]] Vec3 Add(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{ lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

[[nodiscard]] Vec3 Subtract(Vec3 lhs, Vec3 rhs) noexcept {
    return Vec3{ lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

[[nodiscard]] Vec3 ColliderWorldOffset(Vec3 center, Vec3 scale, Quat rotation) noexcept {
    return kb::math::Rotate(rotation, Vec3{
                                          center.x * scale.x,
                                          center.y * scale.y,
                                          center.z * scale.z,
                                      });
}

[[nodiscard]] bool SameVec3(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool IsFinite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsFinite(Quat value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool IsValidQueryShape(const kb::scene::PhysicsShapeDesc& shape) noexcept {
    switch (shape.kind) {
    case kb::scene::PhysicsShapeKind::Sphere:
        return IsPositiveFinite(shape.radius);
    case kb::scene::PhysicsShapeKind::Box:
        return IsPositiveFinite(shape.boxHalfExtents.x) && IsPositiveFinite(shape.boxHalfExtents.y) && IsPositiveFinite(shape.boxHalfExtents.z);
    case kb::scene::PhysicsShapeKind::Capsule:
        return IsPositiveFinite(shape.radius) && IsPositiveFinite(shape.height) &&
            shape.height > shape.radius * 2.0F;
    }
    return false;
}

[[nodiscard]] bool IsNormalized(Quat value) noexcept {
    if (!IsFinite(value)) {
        return false;
    }
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    return std::isfinite(lengthSquared) && std::fabs(lengthSquared - 1.0F) <= 0.001F;
}

[[nodiscard]] float SafeDivide(float value, float divisor) noexcept {
    return std::fabs(divisor) < MinimumShapeExtent ? value : value / divisor;
}

// LIB-133: WriteBack/WriteBackCharacters write a Jolt body's real WORLD-space result - for a
// root entity, local IS world (matches TransformMath::ComposeRoot's own contract), but for a
// child entity under a non-identity parent, writing the world result straight into
// localPosition/localRotation would double-apply the parent transform the next time
// SynchronizeTransformHierarchy recomposes worldPosition from it. This is the exact inverse
// of TransformMath::Compose's forward formula, replicated here (not called directly) because
// TransformMath.hpp is a private kb_engine header this plugin (a separate library) cannot
// include - mirrors the same math kb::script::ScriptTransformApi.cpp's own WorldPoseToLocal
// already uses for Transform.SetWorldPose/SetParent.
[[nodiscard]] Vec3 WorldToLocalPosition(const TransformComponent& parentTransform, Vec3 worldPosition) noexcept {
    const Quat parentRotationInverse = kb::math::Inverse(parentTransform.worldRotation);
    const Vec3 unrotatedDelta = kb::math::Rotate(parentRotationInverse, Subtract(worldPosition, parentTransform.worldPosition));
    return Vec3{
        SafeDivide(unrotatedDelta.x, parentTransform.worldScale.x),
        SafeDivide(unrotatedDelta.y, parentTransform.worldScale.y),
        SafeDivide(unrotatedDelta.z, parentTransform.worldScale.z),
    };
}

[[nodiscard]] Quat WorldToLocalRotation(const TransformComponent& parentTransform, Quat worldRotation) noexcept {
    return kb::math::Inverse(parentTransform.worldRotation) * worldRotation;
}

// Write-back is deliberately split in two phases. First every simulated body/character publishes
// its NEW world pose from this same Jolt step. Only then may a child derive its local pose from
// its parent's world pose. A one-pass unordered_map traversal made dynamic parent+child results
// depend on which record happened to be visited first.
void StageWriteBackWorldPose(TransformComponent& transform, Vec3 worldPosition, Quat worldRotation) noexcept {
    transform.worldPosition = worldPosition;
    transform.worldRotation = worldRotation;
    transform.worldDirty = true;
}

// Honest fallback to "local == world" when the entity has no parent OR its parent's
// TransformComponent cannot be found (matches TransformMath::ComposeRoot's own contract; a
// vanished parent mid-frame is the same shape of edge case CreateBody/SynchronizeBody already
// treats as "not simulated this step", not a crash).
void FinalizeWriteBackLocalPose(SceneSystemContext& context, SceneEntity entity, TransformComponent& transform) {
    const SceneEntity parent = context.GetScene().Hierarchy().Parent(entity);
    const TransformComponent* parentTransform = parent.IsValid() ? context.Transforms().TryGet(parent) : nullptr;
    if (parentTransform != nullptr) {
        transform.localPosition = WorldToLocalPosition(*parentTransform, transform.worldPosition);
        transform.localRotation = WorldToLocalRotation(*parentTransform, transform.worldRotation);
    } else {
        transform.localPosition = transform.worldPosition;
        transform.localRotation = transform.worldRotation;
    }
    transform.worldDirty = true;
    context.Transforms().MarkModified(entity);
}

[[nodiscard]] BodySignature MakeSignature(const RigidbodyComponent& rigidbody, const ColliderComponent& collider, const TransformComponent& transform) noexcept {
    return BodySignature{
        .bodyType = rigidbody.bodyType,
        .shape = collider.shape,
        .scale = transform.worldScale,
        .center = collider.center,
        .boxSize = collider.boxSize,
        .radius = collider.radius,
        .height = collider.height,
        .mass = rigidbody.mass,
        .gravityScale = rigidbody.gravityScale,
        .useGravity = rigidbody.useGravity,
        .lockRotation = rigidbody.lockRotation,
        .useContinuousCollision = rigidbody.useContinuousCollision,
        .trigger = collider.trigger,
        .friction = collider.friction,
        .restitution = collider.restitution,
        .layer = collider.layer,
    };
}

[[nodiscard]] bool operator==(const BodySignature& lhs, const BodySignature& rhs) noexcept {
    return lhs.bodyType == rhs.bodyType && lhs.shape == rhs.shape && SameVec3(lhs.scale, rhs.scale) &&
        SameVec3(lhs.center, rhs.center) && SameVec3(lhs.boxSize, rhs.boxSize) && lhs.radius == rhs.radius &&
        lhs.height == rhs.height && lhs.mass == rhs.mass && lhs.gravityScale == rhs.gravityScale &&
        lhs.useGravity == rhs.useGravity && lhs.lockRotation == rhs.lockRotation &&
        lhs.useContinuousCollision == rhs.useContinuousCollision && lhs.trigger == rhs.trigger &&
        lhs.layer == rhs.layer &&
        lhs.friction == rhs.friction && lhs.restitution == rhs.restitution;
}

// LIB-130: which real Jolt body(-ies) a JointComponent's constraint was last
// built against, alongside the component's own data - if EITHER changes
// (the joint's own fields edited, or the owner/connected entity's body was
// recreated - e.g. its RigidbodyComponent/ColliderComponent changed, see
// BodySignature above), the constraint must be destroyed and rebuilt rather
// than left pointing at stale Jolt bodies.
struct JointSignature {
    JointType type = JointType::Fixed;
    SceneEntity connectedEntity{};
    Vec3 anchor{};
    Vec3 connectedAnchor{};
    Vec3 axis{};
    float minLimit = 0.0F;
    float maxLimit = 0.0F;
    bool enableLimit = false;
    JPH::BodyID ownerBodyId;
    JPH::BodyID connectedBodyId; // invalid BodyID when connectedEntity is invalid (joint connects to the static world).
};

[[nodiscard]] JointSignature MakeJointSignature(const JointComponent& joint, JPH::BodyID ownerBodyId, JPH::BodyID connectedBodyId) noexcept {
    return JointSignature{
        .type = joint.type,
        .connectedEntity = joint.connectedEntity,
        .anchor = joint.anchor,
        .connectedAnchor = joint.connectedAnchor,
        .axis = joint.axis,
        .minLimit = joint.minLimit,
        .maxLimit = joint.maxLimit,
        .enableLimit = joint.enableLimit,
        .ownerBodyId = ownerBodyId,
        .connectedBodyId = connectedBodyId,
    };
}

[[nodiscard]] bool operator==(const JointSignature& lhs, const JointSignature& rhs) noexcept {
    return lhs.type == rhs.type && lhs.connectedEntity == rhs.connectedEntity && SameVec3(lhs.anchor, rhs.anchor) &&
        SameVec3(lhs.connectedAnchor, rhs.connectedAnchor) && SameVec3(lhs.axis, rhs.axis) &&
        lhs.minLimit == rhs.minLimit && lhs.maxLimit == rhs.maxLimit && lhs.enableLimit == rhs.enableLimit &&
        lhs.ownerBodyId == rhs.ownerBodyId && lhs.connectedBodyId == rhs.connectedBodyId;
}

struct JointRecord {
    JPH::Ref<JPH::Constraint> constraint;
    JointSignature signature;
};

// LIB-130: builds the real Jolt constraint for one of the "faktycznie
// obslugiwane typy" (Fixed/Hinge/Distance/Point - JointComponent::type's
// full enum, all four have a direct Jolt TwoBodyConstraint equivalent, so
// none are excluded). mSpace=LocalToBodyCOM makes anchor/connectedAnchor
// local offsets on each body, matching JointComponent's own doc comment;
// every collider shape this engine creates (CreateShape above) is centered
// on its body's origin, so "local to body COM" and "local to body origin"
// coincide here - no extra center-of-mass correction is needed. Fixed and
// Point have no limit concept at all (a rigid weld and a free-swinging ball
// joint respectively); only Hinge (swing angle) and Distance
// (min/max separation) honor JointComponent::enableLimit/minLimit/maxLimit,
// mirroring that component's own doc comment on which fields apply to which
// type.
[[nodiscard]] JPH::Ref<JPH::Constraint> CreateJointConstraint(const JointComponent& joint, JPH::Body& body1, JPH::Body& body2) {
    switch (joint.type) {
    case JointType::Fixed: {
        JPH::FixedConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
        settings.mPoint1 = ToJoltPosition(joint.anchor);
        settings.mPoint2 = ToJoltPosition(joint.connectedAnchor);
        return settings.Create(body1, body2);
    }
    case JointType::Hinge: {
        JPH::HingeConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
        settings.mPoint1 = ToJoltPosition(joint.anchor);
        settings.mPoint2 = ToJoltPosition(joint.connectedAnchor);
        const JPH::Vec3 hingeAxis = ToJolt(joint.axis).NormalizedOr(JPH::Vec3::sAxisY());
        settings.mHingeAxis1 = hingeAxis;
        settings.mHingeAxis2 = hingeAxis;
        const JPH::Vec3 normalAxis = hingeAxis.GetNormalizedPerpendicular();
        settings.mNormalAxis1 = normalAxis;
        settings.mNormalAxis2 = normalAxis;
        if (joint.enableLimit) {
            // Jolt's own documented range: mLimitsMin in [-pi, 0], mLimitsMax in [0, pi].
            settings.mLimitsMin = kb::math::Clamp(JPH::DegreesToRadians(joint.minLimit), -JPH::JPH_PI, 0.0F);
            settings.mLimitsMax = kb::math::Clamp(JPH::DegreesToRadians(joint.maxLimit), 0.0F, JPH::JPH_PI);
        }
        return settings.Create(body1, body2);
    }
    case JointType::Distance: {
        JPH::DistanceConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
        settings.mPoint1 = ToJoltPosition(joint.anchor);
        settings.mPoint2 = ToJoltPosition(joint.connectedAnchor);
        if (joint.enableLimit) {
            settings.mMinDistance = joint.minLimit;
            settings.mMaxDistance = joint.maxLimit;
        }
        // enableLimit==false leaves Jolt's own default (-1/-1), which it
        // documents as "replaced by the distance between mPoint1 and
        // mPoint2" - i.e. rigidly holds whatever separation the joint was
        // created at, the honest "no limit configured" behavior.
        return settings.Create(body1, body2);
    }
    case JointType::Point: {
        JPH::PointConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
        settings.mPoint1 = ToJoltPosition(joint.anchor);
        settings.mPoint2 = ToJoltPosition(joint.connectedAnchor);
        return settings.Create(body1, body2);
    }
    }
    return {};
}

// LIB-131: which real shape a CharacterControllerComponent's JPH::CharacterVirtual was last
// built against (mirror BodySignature) - only a SHAPE change (center/radius/height/scale)
// forces destroy+recreate; slopeLimitDegrees is applied live every step via
// JPH::CharacterVirtual::SetMaxSlopeAngle (see UpdateCharacters below) and needs no rebuild.
struct CharacterSignature {
    Vec3 scale{ 1.0F, 1.0F, 1.0F };
    Vec3 center{};
    float radius = 0.5F;
    float height = 2.0F;
};

[[nodiscard]] CharacterSignature MakeCharacterSignature(const CharacterControllerComponent& character, const TransformComponent& transform) noexcept {
    return CharacterSignature{
        .scale = transform.worldScale,
        .center = character.center,
        .radius = character.radius,
        .height = character.height,
    };
}

[[nodiscard]] bool operator==(const CharacterSignature& lhs, const CharacterSignature& rhs) noexcept {
    return SameVec3(lhs.scale, rhs.scale) && SameVec3(lhs.center, rhs.center) && lhs.radius == rhs.radius && lhs.height == rhs.height;
}

struct CharacterRecord {
    JPH::Ref<JPH::CharacterVirtual> character;
    CharacterSignature signature{};
    // LIB-131: per-frame INPUT set by Physics.CharacterMove/CharacterJump (see
    // IPhysicsBackend's own doc comment for why this cannot live on the component like
    // Rigidbody/Joint data does) - consumed every fixed step by UpdateCharacters below.
    // pendingJumpSpeed is cleared back to 0 every step regardless of whether the jump was
    // actually honored (character not grounded), matching a real jump button's one-shot
    // semantics.
    Vec3 moveInput{};
    float pendingJumpSpeed = 0.0F;
    // Mirrors of CharacterControllerComponent's own fields that Jolt has no "live setter"
    // for (unlike slopeLimitDegrees, applied straight to the JPH::CharacterVirtual via
    // SetMaxSlopeAngle - see SynchronizeCharacter) - refreshed every step regardless of
    // whether the character's SHAPE changed, so editing them never requires a rebuild.
    float stepOffset = 0.4F;
    float gravityScale = 1.0F;
    bool useGravity = true;
};

struct RootMotionSegment {
    Vec3 localTranslation{};
    Quat localRotation{};
    float durationSeconds = 0.0F;
    std::uint64_t animatorRuntimeGeneration = 0U;
};

using RootMotionQueue = std::deque<RootMotionSegment>;

[[nodiscard]] Quat QueuedEndRotation(
    Quat worldRotation, const RootMotionQueue* queue) noexcept {
    if (queue != nullptr) {
        for (const RootMotionSegment& segment : *queue) {
            worldRotation = kb::math::Normalize(
                worldRotation * segment.localRotation);
        }
    }
    return worldRotation;
}

[[nodiscard]] bool IsWorldUpright(Quat rotation) noexcept {
    const Vec3 up = kb::math::Rotate(rotation, Vec3{ 0.0F, 1.0F, 0.0F });
    return std::abs(up.x) <= 1.0e-5F &&
        std::abs(up.z) <= 1.0e-5F &&
        up.y >= 1.0F - 1.0e-5F;
}

[[nodiscard]] RootMotionSegment ComposeRootMotion(
    const RootMotionSegment& lhs, const RootMotionSegment& rhs) noexcept {
    return RootMotionSegment{
        .localTranslation = lhs.localTranslation +
            kb::math::Rotate(lhs.localRotation, rhs.localTranslation),
        .localRotation = kb::math::Normalize(lhs.localRotation * rhs.localRotation),
        .durationSeconds = lhs.durationSeconds + rhs.durationSeconds,
        .animatorRuntimeGeneration = rhs.animatorRuntimeGeneration,
    };
}

[[nodiscard]] RootMotionSegment RelativeRootMotion(
    const RootMotionSegment& from, const RootMotionSegment& to) noexcept {
    const Quat inverse = kb::math::Inverse(from.localRotation);
    return RootMotionSegment{
        .localTranslation = kb::math::Rotate(
            inverse, to.localTranslation - from.localTranslation),
        .localRotation = kb::math::Normalize(inverse * to.localRotation),
        .durationSeconds = std::max(0.0F, to.durationSeconds - from.durationSeconds),
        .animatorRuntimeGeneration = to.animatorRuntimeGeneration,
    };
}

[[nodiscard]] RootMotionSegment ConsumeRootMotion(
    RootMotionQueue& queue, float durationSeconds) noexcept {
    RootMotionSegment consumed{};
    float remaining = durationSeconds;
    while (remaining > 1.0e-7F && !queue.empty()) {
        RootMotionSegment& segment = queue.front();
        if (segment.durationSeconds <= remaining + 1.0e-7F) {
            consumed = ComposeRootMotion(consumed, segment);
            remaining = std::max(0.0F, remaining - segment.durationSeconds);
            queue.pop_front();
            continue;
        }

        const float fraction = std::clamp(
            remaining / segment.durationSeconds, 0.0F, 1.0F);
        const RootMotionSegment slice{
            .localTranslation = segment.localTranslation * fraction,
            .localRotation = kb::math::Slerp(Quat{}, segment.localRotation, fraction),
            .durationSeconds = remaining,
            .animatorRuntimeGeneration = segment.animatorRuntimeGeneration,
        };
        segment = RelativeRootMotion(slice, segment);
        consumed = ComposeRootMotion(consumed, slice);
        remaining = 0.0F;
    }
    return consumed;
}

[[nodiscard]] JPH::EMotionType ToMotionType(RigidbodyBodyType bodyType) noexcept {
    switch (bodyType) {
    case RigidbodyBodyType::Static:
        return JPH::EMotionType::Static;
    case RigidbodyBodyType::Kinematic:
        return JPH::EMotionType::Kinematic;
    case RigidbodyBodyType::Dynamic:
        return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Dynamic;
}

// LIB-131: factored out of the Capsule case below so CharacterControllerComponent's shape
// (SynchronizeCharacter's CreateCharacterSettings) can build the identical capsule geometry
// instead of re-deriving the half-cylinder math a second time.
[[nodiscard]] JPH::RefConst<JPH::Shape> CreateCapsuleShape(float scaledRadius, float scaledHeight) {
    const float radius = ClampPositive(scaledRadius);
    const float clampedHeight = ClampPositive(scaledHeight);
    const float halfCylinder = std::max(0.0F, (clampedHeight * 0.5F) - radius);
    return new JPH::CapsuleShape(halfCylinder, radius);
}

[[nodiscard]] JPH::RefConst<JPH::Shape> CreateShape(const ColliderComponent& collider, Vec3 scale) {
    const float scaleX = AbsScale(scale.x);
    const float scaleY = AbsScale(scale.y);
    const float scaleZ = AbsScale(scale.z);

    switch (collider.shape) {
    case ColliderShape::Sphere:
        return new JPH::SphereShape(ClampPositive(collider.radius * std::max({ scaleX, scaleY, scaleZ })));
    case ColliderShape::Capsule:
        return CreateCapsuleShape(collider.radius * std::max(scaleX, scaleZ), collider.height * scaleY);
    case ColliderShape::Box:
        return new JPH::BoxShape(JPH::Vec3(
            ClampPositive(collider.boxSize.x * scaleX * 0.5F),
            ClampPositive(collider.boxSize.y * scaleY * 0.5F),
            ClampPositive(collider.boxSize.z * scaleZ * 0.5F)));
    }
    return new JPH::BoxShape(JPH::Vec3(0.5F, 0.5F, 0.5F));
}

struct PhysicsBodySnapshot {
    SceneEntity entity{};
    TransformComponent transform{};
    RigidbodyComponent rigidbody{};
    ColliderComponent collider{};
};

using PhysicsBodyQuery = kb::ecs::Query<TransformComponent, RigidbodyComponent, ColliderComponent>;

// Unity-like: an entity with a Collider but NO Rigidbody is an implicit STATIC
// body (a plain static collider). This query finds every collider; the ones that
// also have a Rigidbody are skipped (handled by PhysicsBodyQuery with their real
// Rigidbody), and the rest get a synthesized Static RigidbodyComponent.
using ColliderOnlyBodyQuery = kb::ecs::Query<TransformComponent, ColliderComponent>;

// LIB-131: a CharacterControllerComponent entity deliberately never appears in
// PhysicsBodyQuery above (it has no Rigidbody/Collider - the whole point of a character
// controller is a shape that moves itself via collision sweeps instead of being simulated
// as an ordinary Body), so it needs its own, entirely separate query/snapshot/synchronize
// path rather than sharing bodies_.
struct CharacterSnapshot {
    SceneEntity entity{};
    TransformComponent transform{};
    CharacterControllerComponent character{};
};

using CharacterQuery = kb::ecs::Query<TransformComponent, CharacterControllerComponent>;

struct JointSnapshot {
    SceneEntity entity{};
    JointComponent joint{};
};

using JointQuery = kb::ecs::Query<JointComponent>;

// Query-time layer mask filter (LIB-125): every body's mUserData is set to
// its ColliderComponent::layer bitmask at creation time (CreateBody below) -
// Jolt's own ObjectLayer/ObjectLayerFilter is a single coarse value (a named
// layer + static/dynamic sub-lane, see ToObjectLayer above; still not an
// arbitrary per-body bitmask) so BodyFilter::ShouldCollideLocked (which
// runs after the body is locked, giving access to GetUserData()) remains the
// correct extension point for a query's own layerMask - orthogonal to
// LIB-129's interaction matrix, which governs real collision RESPONSE via
// ObjectLayerPairFilter instead.
class LayerMaskBodyFilter final : public JPH::BodyFilter {
public:
    LayerMaskBodyFilter(std::uint32_t mask, const std::unordered_map<JPH::BodyID, SceneEntity>& entityByBodyId, const std::unordered_map<std::uint64_t, BodyRecord>& bodies, kb::scene::Scene& scene) noexcept
        : mask_(mask)
        , entityByBodyId_(entityByBodyId)
        , bodies_(bodies)
        , scene_(scene) {}

    [[nodiscard]] bool ShouldCollide(const JPH::BodyID& bodyId) const override {
        return ColliderForBody(bodyId) != nullptr;
    }

    [[nodiscard]] bool ShouldCollideLocked(const JPH::Body& body) const override {
        const ColliderComponent* collider = ColliderForBody(body.GetID());
        return collider != nullptr && (collider->layer & mask_) != 0U;
    }

private:
    [[nodiscard]] const ColliderComponent* ColliderForBody(const JPH::BodyID& bodyId) const noexcept {
        const auto entityIt = entityByBodyId_.find(bodyId);
        if (entityIt == entityByBodyId_.end() || !scene_.Entities().IsAlive(entityIt->second) || scene_.Transforms().TryGet(entityIt->second) == nullptr) {
            return nullptr;
        }
        const ColliderComponent* collider = scene_.Components().Colliders().TryGet(entityIt->second);
        if (collider == nullptr) {
            return nullptr;
        }
        const auto bodyIt = bodies_.find(entityIt->second.Id());
        if (bodyIt == bodies_.end() || bodyIt->second.bodyId != bodyId) {
            return nullptr;
        }
        const TransformComponent* transform = scene_.Transforms().TryGet(entityIt->second);
        const RigidbodyComponent* rigidbody = scene_.Components().Rigidbodies().TryGet(entityIt->second);
        RigidbodyComponent implicitStatic;
        implicitStatic.bodyType = RigidbodyBodyType::Static;
        return bodyIt->second.signature == MakeSignature(rigidbody != nullptr ? *rigidbody : implicitStatic, *collider, *transform) ? collider : nullptr;
    }

    std::uint32_t mask_ = 0U;
    const std::unordered_map<JPH::BodyID, SceneEntity>& entityByBodyId_;
    const std::unordered_map<std::uint64_t, BodyRecord>& bodies_;
    kb::scene::Scene& scene_;
};

template <typename Result, typename Less>
void InsertUniqueBounded(
    kb::library::ArrayNonAlloc<Result>& results,
    Result candidate,
    Less less) {
    for (std::size_t index = 0U; index < results.Count(); ++index) {
        const Result* existing = results.GetAt(index);
        if (existing != nullptr && existing->entity == candidate.entity) {
            if (!less(candidate, *existing)) {
                return;
            }
            [[maybe_unused]] const bool removed = results.RemoveAt(index);
            break;
        }
    }

    const std::size_t count = results.Count();
    const std::size_t capacity = results.Capacity();
    if (capacity == 0U) {
        return;
    }
    std::size_t insertAt = 0U;
    while (insertAt < count && !less(candidate, *results.GetAt(insertAt))) {
        ++insertAt;
    }
    if (insertAt >= capacity) {
        return;
    }

    if (count < capacity) {
        [[maybe_unused]] const bool appended = results.PushBack(candidate);
        for (std::size_t index = count; index > insertAt; --index) {
            [[maybe_unused]] const bool shifted = results.SetAt(index, *results.GetAt(index - 1U));
        }
    } else {
        for (std::size_t index = count - 1U; index > insertAt; --index) {
            [[maybe_unused]] const bool shifted = results.SetAt(index, *results.GetAt(index - 1U));
        }
    }
    [[maybe_unused]] const bool inserted = results.SetAt(insertAt, candidate);
}

class NonAllocCastShapeCollector final : public JPH::CastShapeCollector {
public:
    NonAllocCastShapeCollector(
        const std::unordered_map<JPH::BodyID, SceneEntity>& entityByBodyId,
        Vec3 origin,
        float maxDistance,
        kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult>& results) noexcept
        : entityByBodyId_(entityByBodyId)
        , origin_(origin)
        , maxDistance_(maxDistance)
        , results_(results) {}

    void AddHit(const ResultType& hit) override {
        const auto entityIt = entityByBodyId_.find(hit.mBodyID2);
        if (entityIt == entityByBodyId_.end()) {
            return;
        }
        InsertUniqueBounded(
            results_,
            kb::scene::PhysicsCastResult{
                .hit = true,
                .entity = entityIt->second,
                .distance = hit.mFraction * maxDistance_,
                .point = Add(origin_, FromJolt(hit.mContactPointOn2)),
                .normal = FromJolt(-hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero())),
            },
            [](const kb::scene::PhysicsCastResult& lhs, const kb::scene::PhysicsCastResult& rhs) {
                return lhs.distance < rhs.distance ||
                    (lhs.distance == rhs.distance && lhs.entity.Id() < rhs.entity.Id());
            });
    }

private:
    const std::unordered_map<JPH::BodyID, SceneEntity>& entityByBodyId_;
    Vec3 origin_{};
    float maxDistance_ = 0.0F;
    kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult>& results_;
};

class NonAllocOverlapShapeCollector final : public JPH::CollideShapeCollector {
public:
    NonAllocOverlapShapeCollector(
        const std::unordered_map<JPH::BodyID, SceneEntity>& entityByBodyId,
        kb::library::ArrayNonAlloc<kb::scene::PhysicsOverlapResult>& results) noexcept
        : entityByBodyId_(entityByBodyId)
        , results_(results) {}

    void AddHit(const ResultType& hit) override {
        const auto entityIt = entityByBodyId_.find(hit.mBodyID2);
        if (entityIt == entityByBodyId_.end()) {
            return;
        }
        InsertUniqueBounded(
            results_,
            kb::scene::PhysicsOverlapResult{
                .overlapping = true,
                .entity = entityIt->second,
                .penetrationDepth = std::max(0.0F, hit.mPenetrationDepth),
            },
            [](const kb::scene::PhysicsOverlapResult& lhs, const kb::scene::PhysicsOverlapResult& rhs) {
                return lhs.penetrationDepth > rhs.penetrationDepth ||
                    (lhs.penetrationDepth == rhs.penetrationDepth && lhs.entity.Id() < rhs.entity.Id());
            });
    }

private:
    const std::unordered_map<JPH::BodyID, SceneEntity>& entityByBodyId_;
    kb::library::ArrayNonAlloc<kb::scene::PhysicsOverlapResult>& results_;
};

template <typename Callback>
void WithQueryShape(const kb::scene::PhysicsShapeDesc& shape, Callback&& callback) {
    switch (shape.kind) {
    case kb::scene::PhysicsShapeKind::Sphere: {
        const JPH::SphereShape queryShape(shape.radius);
        callback(queryShape);
        return;
    }
    case kb::scene::PhysicsShapeKind::Box: {
        const JPH::BoxShape queryShape(ToJolt(shape.boxHalfExtents));
        callback(queryShape);
        return;
    }
    case kb::scene::PhysicsShapeKind::Capsule: {
        const JPH::CapsuleShape queryShape((shape.height * 0.5F) - shape.radius, shape.radius);
        callback(queryShape);
        return;
    }
    }
}

// LIB-127: OnContactAdded/Persisted/Removed genuinely fire from MULTIPLE
// Jolt job-system threads at once (ContactListener.h's own doc comment:
// "callbacks... are called from multiple threads at the same time when all
// bodies are locked"), so the listener may only read the minimum safe data
// (BodyID/IsSensor/manifold point+normal - never lock/mutate bodies) and
// must buffer it behind a mutex. Resolving BodyID -> SceneEntity and
// building the actual queued event both happen later, on the main thread,
// after Step() returns (entityByBodyId_ is not thread-safe and must never
// be touched from these callbacks).
enum class RawContactPhase : std::uint8_t {
    Added,
    Persisted,
    Removed,
};

struct RawContactEvent {
    JPH::SubShapeIDPair pair;
    JPH::BodyID body1;
    JPH::BodyID body2;
    bool isSensor1 = false;
    bool isSensor2 = false;
    RawContactPhase phase = RawContactPhase::Added;
    Vec3 point{};
    Vec3 normal{};
};

struct BodyContactKey {
    JPH::BodyID body1;
    JPH::BodyID body2;

    [[nodiscard]] friend bool operator<(const BodyContactKey& lhs, const BodyContactKey& rhs) noexcept {
        return lhs.body1 < rhs.body1 || (lhs.body1 == rhs.body1 && lhs.body2 < rhs.body2);
    }
};

struct ContactPayload {
    Vec3 point{};
    Vec3 normal{};
    bool isTrigger = false;
};

struct ActiveBodyContact {
    // A compound shape can produce several simultaneous SubShapeIDPair
    // contacts for the same two bodies. Script callbacks are body/entity
    // callbacks, so this set is the authoritative aggregation boundary:
    // Enter is emitted when it changes empty -> non-empty and Exit only
    // when the final sub-shape contact disappears.
    std::map<JPH::SubShapeIDPair, ContactPayload> subShapes;
};

class JoltCollisionContactListener final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override {
        static_cast<void>(settings);
        Record(body1, body2, manifold, RawContactPhase::Added);
    }

    void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override {
        static_cast<void>(settings);
        Record(body1, body2, manifold, RawContactPhase::Persisted);
    }

    // Cannot access body/manifold data here at all (Jolt's own doc: "You
    // cannot access the bodies at the time of this callback... the body may
    // have been removed and destroyed") - point/normal stay zero for Exit
    // events; only the two BodyIDs (still valid to read from the pair
    // itself) are available.
    void OnContactRemoved(const JPH::SubShapeIDPair& subShapePair) override {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(RawContactEvent{
            .pair = subShapePair,
            .body1 = subShapePair.GetBody1ID(),
            .body2 = subShapePair.GetBody2ID(),
            .phase = RawContactPhase::Removed,
        });
    }

    [[nodiscard]] std::vector<RawContactEvent> DrainAndClear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<RawContactEvent> drained;
        drained.swap(pending_);
        return drained;
    }

    void DiscardBody(JPH::BodyID bodyId) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.erase(
            std::remove_if(
                pending_.begin(),
                pending_.end(),
                [bodyId](const RawContactEvent& event) {
                    return event.body1 == bodyId || event.body2 == bodyId;
                }),
            pending_.end());
    }

private:
    void Record(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, RawContactPhase phase) {
        Vec3 point{};
        if (!manifold.mRelativeContactPointsOn1.empty()) {
            point = FromJoltPosition(manifold.GetWorldSpaceContactPointOn1(0));
        }
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(RawContactEvent{
            .pair = JPH::SubShapeIDPair(body1.GetID(), manifold.mSubShapeID1, body2.GetID(), manifold.mSubShapeID2),
            .body1 = body1.GetID(),
            .body2 = body2.GetID(),
            .isSensor1 = body1.IsSensor(),
            .isSensor2 = body2.IsSensor(),
            .phase = phase,
            .point = point,
            .normal = FromJolt(manifold.mWorldSpaceNormal),
        });
    }

    std::mutex mutex_;
    std::vector<RawContactEvent> pending_;
};

} // namespace

class JoltPhysicsSceneSystem::Impl final : public kb::scene::IPhysicsBackend {
public:
    explicit Impl(JoltPhysicsSceneSystemSettings settings)
        : settings_(settings)
        , broadPhaseLayers_(kObjectLayerCount, BroadPhaseLayers::Count)
        , objectLayerPairFilter_(kObjectLayerCount)
        , objectVsBroadPhaseFilter_(objectLayerPairFilter_, broadPhaseLayers_)
        , tempAllocator_(10U * 1024U * 1024U)
        , jobSystem_(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, WorkerThreadCount()) {
        for (JPH::ObjectLayer namedLayer = 0; namedLayer < static_cast<JPH::ObjectLayer>(kNamedLayerCount); ++namedLayer) {
            broadPhaseLayers_.MapObjectToBroadPhaseLayer(ToObjectLayer(namedLayer, true), BroadPhaseLayers::NonMoving);
            broadPhaseLayers_.MapObjectToBroadPhaseLayer(ToObjectLayer(namedLayer, false), BroadPhaseLayers::Moving);
        }
        // LIB-129: every layer interacts with every other layer by default,
        // matching this engine's behavior before LIB-129 (static vs. dynamic
        // affected only broad-phase pruning, never collision response) - a
        // project that never calls ConfigureLayers sees this default
        // forever, and every collider that never set an explicit
        // ColliderComponent::layer resolves to named layer 0 (see
        // kb::scene::LowestSetPhysicsLayerIndex), so existing content is
        // unaffected either way.
        for (JPH::ObjectLayer first = 0; first < static_cast<JPH::ObjectLayer>(kObjectLayerCount); ++first) {
            for (JPH::ObjectLayer second = first; second < static_cast<JPH::ObjectLayer>(kObjectLayerCount); ++second) {
                objectLayerPairFilter_.EnableCollision(first, second);
            }
        }
        physicsSystem_.Init(MaxBodies, NumBodyMutexes, MaxBodyPairs, MaxContactConstraints, broadPhaseLayers_, objectVsBroadPhaseFilter_, objectLayerPairFilter_);
        physicsSystem_.SetGravity(JPH::Vec3(0.0F, -9.81F, 0.0F));
        // LIB-127: only a single ContactListener can be registered per
        // PhysicsSystem (Jolt's own doc comment) - this Impl owns the one
        // instance for its own PhysicsSystem, exactly like it owns
        // broadPhaseLayers_/objectVsBroadPhaseFilter_ above.
        physicsSystem_.SetContactListener(&contactListener_);
    }

    // LIB-129: see IPhysicsBackend::ConfigureLayers - mutates
    // objectLayerPairFilter_ in place (JPH::ObjectLayerPairFilterTable's
    // EnableCollision/DisableCollision are plain, non-virtual methods
    // queried live by Jolt's narrow-phase AND by this Impl's own
    // ObjectVsBroadPhaseLayerFilter above), so this is safe to call at any
    // point in Impl's lifetime, before or after Init/bodies exist - no
    // re-Init of physicsSystem_ needed.
    bool ConfigureLayers(const kb::scene::PhysicsLayersAsset& layers) noexcept override {
        for (std::uint32_t a = 0U; a < kb::scene::kPhysicsLayerCount; ++a) {
            for (std::uint32_t b = a; b < kb::scene::kPhysicsLayerCount; ++b) {
                const bool interact = layers.LayersInteract(a, b);
                for (const bool aStatic : { true, false }) {
                    for (const bool bStatic : { true, false }) {
                        const JPH::ObjectLayer objectA = ToObjectLayer(a, aStatic);
                        const JPH::ObjectLayer objectB = ToObjectLayer(b, bStatic);
                        if (interact) {
                            objectLayerPairFilter_.EnableCollision(objectA, objectB);
                        } else {
                            objectLayerPairFilter_.DisableCollision(objectA, objectB);
                        }
                    }
                }
            }
        }
        return true;
    }

    ~Impl() override {
        RemoveAllCharacters();
        RemoveAllJoints();
        RemoveAllBodies();
    }

    // LIB-131: UpdateCharacters runs BEFORE Step() below, matching Jolt's own reference
    // sample (Samples/Tests/Character/CharacterVirtualTest.cpp names this ordering
    // "PrePhysicsUpdate") - a character's ExtendedUpdate reads ground bodies' CURRENT
    // (not-yet-this-frame-advanced) velocity to compute how far to ride along with them,
    // and Step() then advances those same ground bodies by that same velocity*dt - running
    // the character after Step() instead would double-count (or lag a frame behind) however
    // far a platform the character is standing on moves this step.
    void OnFixedUpdate(SceneSystemContext& context) {
        SynchronizeBodies(context);
        ApplyPendingRigidbodyRootMotion(context);
        SynchronizeJoints(context);
        SynchronizeCharacters(context);
        UpdateCharacters(context);
        Step(context.DeltaSeconds());
        writeBackEntities_.clear();
        writeBackEntities_.reserve(bodies_.size() + characters_.size());
        WriteBack(context);
        WriteBackCharacters(context);
        FinalizeWriteBackLocalPoses(context);
        DispatchContactEvents(context);
    }

    void ApplyPendingRigidbodyRootMotion(SceneSystemContext& context) {
        const float deltaSeconds = context.DeltaSeconds();
        if (deltaSeconds <= 0.0F) return;
        for (auto it = pendingRigidbodyRootMotion_.begin(); it != pendingRigidbodyRootMotion_.end();) {
            const SceneEntity entity{ it->first };
            const kb::scene::Animator* animator =
                context.GetScene().Components().Animators().TryGet(entity);
            const RigidbodyComponent* rigidbody = context.GetScene().Components().Rigidbodies().TryGet(entity);
            const TransformComponent* transform = context.Transforms().TryGet(entity);
            const auto body = bodies_.find(it->first);
            if (animator == nullptr ||
                !animator->enabled ||
                !context.GetScene().Entities().IsActive(entity) ||
                animator->rootMotionOwner != kb::scene::AnimatorRootMotionOwner::Rigidbody ||
                it->second.empty() ||
                it->second.front().animatorRuntimeGeneration !=
                    context.GetScene().Animators().RuntimeBindingGeneration(entity) ||
                context.GetScene().Components().CharacterControllers().TryGet(entity) != nullptr ||
                rigidbody == nullptr || transform == nullptr ||
                rigidbody->bodyType != RigidbodyBodyType::Kinematic) {
                it = pendingRigidbodyRootMotion_.erase(it);
                continue;
            }
            if (body == bodies_.end()) {
                ++it;
                continue;
            }

            BodyRecord* bodyRecord = FindKinematicBody(entity);
            if (bodyRecord == nullptr) {
                ++it;
                continue;
            }
            const RootMotionSegment motion = ConsumeRootMotion(it->second, deltaSeconds);
            const Vec3 worldTranslation = kb::math::Rotate(transform->worldRotation, motion.localTranslation);
            const Quat targetRotation = kb::math::Normalize(transform->worldRotation * motion.localRotation);
            const Vec3 targetPosition = transform->worldPosition + worldTranslation;
            if (!MoveKinematic(entity, targetPosition, targetRotation, deltaSeconds)) {
                ++it;
                continue;
            }
            // Root motion is queued and applied inside this fixed update,
            // after SynchronizeBodies. Its velocity has exactly this one
            // Step to reach the target, so the generic pre-fixed
            // MoveKinematic preservation flag must not carry it into the
            // following substep and repeat the delta.
            bodyRecord->pendingKinematicMove = false;
            if (it->second.empty()) {
                it = pendingRigidbodyRootMotion_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void OnDestroy() {
        // LIB-130: a constraint references its two bodies internally -
        // remove joints BEFORE the bodies they connect, matching the real
        // dependency order (mirrors why Jolt itself requires
        // RemoveConstraint before the bodies it references are destroyed).
        RemoveAllCharacters();
        RemoveAllJoints();
        RemoveAllBodies();
    }

    void AttachScene(kb::scene::Scene& scene) noexcept {
        scene_ = &scene;
    }

    void DetachScene() noexcept {
        scene_ = nullptr;
    }

    // kb::scene::IPhysicsBackend - LIB-124. Every method looks the entity up
    // in the SAME bodies_ map SynchronizeBodies already maintains; a miss
    // (no live body this frame - not yet synchronized, or the entity has no
    // Rigidbody/Collider at all) is a real, honest "not applied" (false /
    // found=false), never a crash or silent success.
    bool AddForce(SceneEntity entity, Vec3 force) noexcept override {
        const BodyRecord* body = FindDynamicBody(entity);
        if (body == nullptr || !IsFinite(force)) {
            return false;
        }
        physicsSystem_.GetBodyInterface().AddForce(body->bodyId, ToJolt(force));
        return true;
    }

    bool AddImpulse(SceneEntity entity, Vec3 impulse) noexcept override {
        const BodyRecord* body = FindDynamicBody(entity);
        if (body == nullptr || !IsFinite(impulse)) {
            return false;
        }
        physicsSystem_.GetBodyInterface().AddImpulse(body->bodyId, ToJolt(impulse));
        return true;
    }

    bool SetVelocity(SceneEntity entity, Vec3 velocity) noexcept override {
        const BodyRecord* body = FindDynamicBody(entity);
        if (body == nullptr || !IsFinite(velocity)) {
            return false;
        }
        physicsSystem_.GetBodyInterface().SetLinearVelocity(body->bodyId, ToJolt(velocity));
        return true;
    }

    [[nodiscard]] kb::scene::PhysicsVectorResult GetVelocity(SceneEntity entity) const noexcept override {
        const BodyRecord* body = FindDynamicBody(entity);
        if (body == nullptr) {
            return {};
        }
        return kb::scene::PhysicsVectorResult{ .found = true, .value = FromJolt(physicsSystem_.GetBodyInterface().GetLinearVelocity(body->bodyId)) };
    }

    bool SetAngularVelocity(SceneEntity entity, Vec3 angularVelocity) noexcept override {
        const BodyRecord* body = FindDynamicBody(entity);
        if (body == nullptr || !IsFinite(angularVelocity)) {
            return false;
        }
        physicsSystem_.GetBodyInterface().SetAngularVelocity(body->bodyId, ToJolt(angularVelocity));
        return true;
    }

    [[nodiscard]] kb::scene::PhysicsVectorResult GetAngularVelocity(SceneEntity entity) const noexcept override {
        const BodyRecord* body = FindDynamicBody(entity);
        if (body == nullptr) {
            return {};
        }
        return kb::scene::PhysicsVectorResult{ .found = true, .value = FromJolt(physicsSystem_.GetBodyInterface().GetAngularVelocity(body->bodyId)) };
    }

    bool MoveKinematic(SceneEntity entity, Vec3 targetPosition, Quat targetRotation, float deltaSeconds) noexcept override {
        BodyRecord* existing = FindKinematicBody(entity);
        if (existing == nullptr || !IsFinite(targetPosition) || !IsNormalized(targetRotation) || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
            return false;
        }
        const Vec3 targetBodyPosition = Add(targetPosition, ColliderWorldOffset(existing->signature.center, existing->signature.scale, targetRotation));
        physicsSystem_.GetBodyInterface().MoveKinematic(existing->bodyId, ToJoltPosition(targetBodyPosition), ToJolt(targetRotation), deltaSeconds);
        existing->pendingKinematicMove = true;
        return true;
    }

    bool Sleep(SceneEntity entity) noexcept override {
        const BodyRecord* body = FindDynamicBody(entity);
        if (body == nullptr) {
            return false;
        }
        physicsSystem_.GetBodyInterface().DeactivateBody(body->bodyId);
        return true;
    }

    bool Wake(SceneEntity entity) noexcept override {
        const BodyRecord* body = FindDynamicBody(entity);
        if (body == nullptr) {
            return false;
        }
        physicsSystem_.GetBodyInterface().ActivateBody(body->bodyId);
        return true;
    }

    [[nodiscard]] bool IsSleeping(SceneEntity entity) const noexcept override {
        const BodyRecord* body = FindDynamicBody(entity);
        return body != nullptr && !physicsSystem_.GetBodyInterface().IsActive(body->bodyId);
    }

    // LIB-131: see IPhysicsBackend's own doc comment - these operate on characters_ (a
    // JPH::CharacterVirtual per CharacterControllerComponent entity), entirely separate from
    // bodies_ above.
    bool CharacterMove(SceneEntity entity, Vec3 horizontalVelocity) noexcept override {
        CharacterRecord* record = FindCharacterRecord(entity);
        if (record == nullptr) {
            return false;
        }
        record->moveInput = horizontalVelocity;
        return true;
    }

    bool CharacterJump(SceneEntity entity, float verticalSpeed) noexcept override {
        CharacterRecord* record = FindCharacterRecord(entity);
        if (record == nullptr) {
            return false;
        }
        record->pendingJumpSpeed = verticalSpeed;
        return true;
    }

    [[nodiscard]] kb::scene::PhysicsVectorResult CharacterVelocity(SceneEntity entity) const noexcept override {
        const CharacterRecord* record = FindCharacterRecord(entity);
        if (record == nullptr) {
            return {};
        }
        return kb::scene::PhysicsVectorResult{ .found = true, .value = FromJolt(record->character->GetLinearVelocity()) };
    }

    [[nodiscard]] bool CharacterIsGrounded(SceneEntity entity) const noexcept override {
        const CharacterRecord* record = FindCharacterRecord(entity);
        return record != nullptr && record->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    }

    // LIB-131: CharacterBase.h's own doc comment on EGroundState::NotSupported says "The
    // GetGroundXXX functions will return information about the touched object" - so the
    // honest "no ground data" gate is InAir specifically (touching nothing at all), not
    // merely "not fully supported" (which would also exclude the still-meaningful
    // OnSteepGround/NotSupported states).
    [[nodiscard]] kb::scene::PhysicsVectorResult CharacterGroundNormal(SceneEntity entity) const noexcept override {
        const CharacterRecord* record = FindCharacterRecord(entity);
        if (record == nullptr || record->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::InAir) {
            return {};
        }
        return kb::scene::PhysicsVectorResult{ .found = true, .value = FromJolt(record->character->GetGroundNormal()) };
    }

    [[nodiscard]] kb::scene::PhysicsVectorResult CharacterGroundVelocity(SceneEntity entity) const noexcept override {
        const CharacterRecord* record = FindCharacterRecord(entity);
        if (record == nullptr || record->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::InAir) {
            return {};
        }
        return kb::scene::PhysicsVectorResult{ .found = true, .value = FromJolt(record->character->GetGroundVelocity()) };
    }

    bool QueueCharacterRootMotion(
        SceneEntity entity, Vec3 localTranslation, Quat localRotation,
        float durationSeconds) noexcept override {
        if (scene_ == nullptr || !IsFinite(localTranslation) || !IsNormalized(localRotation) ||
            !std::isfinite(durationSeconds) || durationSeconds < 0.0F ||
            !scene_->Entities().IsActive(entity) ||
            scene_->Components().CharacterControllers().TryGet(entity) == nullptr ||
            scene_->Components().Rigidbodies().TryGet(entity) != nullptr ||
            scene_->Components().Colliders().TryGet(entity) != nullptr) {
            return false;
        }
        const kb::scene::Animator* animator = scene_->Components().Animators().TryGet(entity);
        if (animator == nullptr ||
            !animator->enabled ||
            animator->rootMotionOwner != kb::scene::AnimatorRootMotionOwner::CharacterController) {
            return false;
        }
        const std::uint64_t animatorRuntimeGeneration =
            scene_->Animators().RuntimeBindingGeneration(entity);
        if (animatorRuntimeGeneration == 0U) return false;
        const TransformComponent* transform = scene_->Transforms().TryGet(entity);
        auto queued = pendingCharacterRootMotion_.find(entity.Id());
        if (queued != pendingCharacterRootMotion_.end() &&
            !queued->second.empty() &&
            queued->second.front().animatorRuntimeGeneration !=
                animatorRuntimeGeneration) {
            queued->second.clear();
        }
        const RootMotionQueue* queue =
            queued == pendingCharacterRootMotion_.end() ? nullptr : &queued->second;
        const Quat queuedRotation = transform == nullptr
            ? Quat{}
            : QueuedEndRotation(transform->worldRotation, queue);
        if (transform == nullptr || !IsWorldUpright(queuedRotation) ||
            !IsWorldUpright(kb::math::Normalize(queuedRotation * localRotation)) ||
            std::abs(kb::math::Rotate(queuedRotation, localTranslation).y) > 1.0e-5F ||
            (queue != nullptr && queue->size() >= MaxQueuedRootMotionSegments)) {
            return false;
        }
        if (durationSeconds == 0.0F) {
            if (queued != pendingCharacterRootMotion_.end() && queued->second.empty()) {
                pendingCharacterRootMotion_.erase(queued);
            }
            return true;
        }
        RootMotionQueue& mutableQueue = pendingCharacterRootMotion_[entity.Id()];
        mutableQueue.push_back(RootMotionSegment{
            .localTranslation = localTranslation,
            .localRotation = localRotation,
            .durationSeconds = durationSeconds,
            .animatorRuntimeGeneration = animatorRuntimeGeneration,
        });
        return true;
    }

    bool QueueRigidbodyRootMotion(
        SceneEntity entity, Vec3 localTranslation, Quat localRotation,
        float durationSeconds) noexcept override {
        if (scene_ == nullptr || !IsFinite(localTranslation) || !IsNormalized(localRotation) ||
            !std::isfinite(durationSeconds) || durationSeconds < 0.0F ||
            !scene_->Entities().IsActive(entity)) return false;
        const RigidbodyComponent* rigidbody = scene_->Components().Rigidbodies().TryGet(entity);
        const kb::scene::Animator* animator = scene_->Components().Animators().TryGet(entity);
        if (animator == nullptr ||
            !animator->enabled ||
            animator->rootMotionOwner != kb::scene::AnimatorRootMotionOwner::Rigidbody ||
            rigidbody == nullptr || rigidbody->bodyType != RigidbodyBodyType::Kinematic ||
            scene_->Components().Colliders().TryGet(entity) == nullptr ||
            scene_->Components().CharacterControllers().TryGet(entity) != nullptr) {
            return false;
        }
        const std::uint64_t animatorRuntimeGeneration =
            scene_->Animators().RuntimeBindingGeneration(entity);
        if (animatorRuntimeGeneration == 0U) return false;
        auto queued = pendingRigidbodyRootMotion_.find(entity.Id());
        if (queued != pendingRigidbodyRootMotion_.end() &&
            !queued->second.empty() &&
            queued->second.front().animatorRuntimeGeneration !=
                animatorRuntimeGeneration) {
            queued->second.clear();
        }
        if (durationSeconds == 0.0F) {
            if (queued != pendingRigidbodyRootMotion_.end() && queued->second.empty()) {
                pendingRigidbodyRootMotion_.erase(queued);
            }
            return true;
        }
        if (queued != pendingRigidbodyRootMotion_.end() &&
            queued->second.size() >= MaxQueuedRootMotionSegments) {
            return false;
        }
        pendingRigidbodyRootMotion_[entity.Id()].push_back(RootMotionSegment{
            .localTranslation = localTranslation,
            .localRotation = localRotation,
            .durationSeconds = durationSeconds,
            .animatorRuntimeGeneration = animatorRuntimeGeneration,
        });
        return true;
    }

    [[nodiscard]] kb::scene::PhysicsCastResult CastShape(const kb::scene::PhysicsShapeDesc& shape, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask) const noexcept override {
        std::array<kb::scene::PhysicsCastResult, 1U> storage{};
        kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult> results(storage);
        CastShapeAll(shape, origin, direction, maxDistance, layerMask, results);
        return results.Empty() ? kb::scene::PhysicsCastResult{} : *results.GetAt(0U);
    }

    [[nodiscard]] kb::scene::PhysicsOverlapResult OverlapShape(const kb::scene::PhysicsShapeDesc& shape, Vec3 center, std::uint32_t layerMask) const noexcept override {
        std::array<kb::scene::PhysicsOverlapResult, 1U> storage{};
        kb::library::ArrayNonAlloc<kb::scene::PhysicsOverlapResult> results(storage);
        OverlapShapeAll(shape, center, layerMask, results);
        return results.Empty() ? kb::scene::PhysicsOverlapResult{} : *results.GetAt(0U);
    }

    [[nodiscard]] kb::scene::PhysicsClosestPointResult ClosestPoint(SceneEntity entity, Vec3 point, std::uint32_t layerMask) const noexcept override {
        if (!IsFinite(point) || layerMask == 0U || scene_ == nullptr || !scene_->Entities().IsAlive(entity)) {
            return {};
        }
        const ColliderComponent* collider = scene_->Components().Colliders().TryGet(entity);
        if (collider == nullptr || (collider->layer & layerMask) == 0U || scene_->Transforms().TryGet(entity) == nullptr) {
            return {};
        }
        const JPH::BodyID* bodyId = FindCurrentBodyId(entity);
        if (bodyId == nullptr) {
            return {};
        }
        // Jolt exposes no dedicated closest-point query. Query the known
        // target's TransformedShape directly instead of asking NarrowPhase
        // to broaden across the whole world: this keeps a far closest-point
        // request bounded to one body (and avoids a global huge separation
        // radius turning into a pathological broad-phase scan).
        constexpr float PointRadius = 0.01F;
        float maxSearchDistance = 0.0F;
        JPH::TransformedShape targetShape;
        {
            JPH::BodyLockRead bodyLock(physicsSystem_.GetBodyLockInterface(), *bodyId);
            if (!bodyLock.SucceededAndIsInBroadPhase()) {
                return {};
            }
            const JPH::AABox& bounds = bodyLock.GetBody().GetWorldSpaceBounds();
            targetShape = bodyLock.GetBody().GetTransformedShape();
            const JPH::Vec3 queryPoint = ToJolt(point);
            const float extentX = std::max(std::abs(queryPoint.GetX() - bounds.mMin.GetX()), std::abs(queryPoint.GetX() - bounds.mMax.GetX()));
            const float extentY = std::max(std::abs(queryPoint.GetY() - bounds.mMin.GetY()), std::abs(queryPoint.GetY() - bounds.mMax.GetY()));
            const float extentZ = std::max(std::abs(queryPoint.GetZ() - bounds.mMin.GetZ()), std::abs(queryPoint.GetZ() - bounds.mMax.GetZ()));
            maxSearchDistance = std::hypot(extentX, extentY, extentZ) + PointRadius;
        }
        if (!std::isfinite(maxSearchDistance)) {
            return {};
        }
        const JPH::SphereShape pointShape(PointRadius);
        JPH::CollideShapeSettings settings;
        settings.mMaxSeparationDistance = maxSearchDistance;
        JPH::ClosestHitCollisionCollector<JPH::CollideShapeCollector> collector;
        targetShape.CollideShape(
            &pointShape, JPH::Vec3::sReplicate(1.0F), JPH::RMat44::sTranslation(ToJoltPosition(point)), settings, ToJoltPosition(point), collector);
        if (!collector.HadHit()) {
            return {};
        }
        // mContactPointOn2 is relative to inBaseOffset (point, as passed to
        // CollideShape above) - add it back for the absolute world position.
        const Vec3 closest = Add(point, FromJolt(collector.mHit.mContactPointOn2));
        const Vec3 delta = Subtract(closest, point);
        return kb::scene::PhysicsClosestPointResult{
            .found = true,
            .point = closest,
            .distance = std::sqrt((delta.x * delta.x) + (delta.y * delta.y) + (delta.z * delta.z)),
        };
    }

    // LIB-126: same query as CastShape/OverlapShape above, but collecting
    // directly into the caller's ArrayNonAlloc buffer. Query shapes live on
    // the stack and the collectors retain only the best Capacity() hits, so
    // neither query creates Jolt's allocating AllHitCollisionCollector array.
    void CastShapeAll(const kb::scene::PhysicsShapeDesc& shape, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask, kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult>& results) const noexcept override {
        results.Clear();
        if (results.Capacity() == 0U || !IsValidQueryShape(shape) || !IsFinite(origin) || !IsFinite(direction) ||
            !std::isfinite(maxDistance) || maxDistance <= 0.0F || layerMask == 0U || scene_ == nullptr) {
            return;
        }
        const JPH::Vec3 joltDirection = ToJolt(direction);
        const float directionLength = joltDirection.Length();
        if (!std::isfinite(directionLength) || directionLength <= 0.000001F) {
            return;
        }
        WithQueryShape(shape, [&](const JPH::Shape& queryShape) {
            const JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
                &queryShape, JPH::Vec3::sReplicate(1.0F), JPH::RMat44::sTranslation(ToJoltPosition(origin)), (joltDirection / directionLength) * maxDistance);
            JPH::ShapeCastSettings settings;
            NonAllocCastShapeCollector collector(entityByBodyId_, origin, maxDistance, results);
            const LayerMaskBodyFilter bodyFilter(layerMask, entityByBodyId_, bodies_, *scene_);
            physicsSystem_.GetNarrowPhaseQuery().CastShape(shapeCast, settings, ToJoltPosition(origin), collector, {}, {}, bodyFilter);
        });
    }

    void OverlapShapeAll(const kb::scene::PhysicsShapeDesc& shape, Vec3 center, std::uint32_t layerMask, kb::library::ArrayNonAlloc<kb::scene::PhysicsOverlapResult>& results) const noexcept override {
        results.Clear();
        if (results.Capacity() == 0U || !IsValidQueryShape(shape) || !IsFinite(center) ||
            layerMask == 0U || scene_ == nullptr) {
            return;
        }
        WithQueryShape(shape, [&](const JPH::Shape& queryShape) {
            JPH::CollideShapeSettings settings;
            NonAllocOverlapShapeCollector collector(entityByBodyId_, results);
            const LayerMaskBodyFilter bodyFilter(layerMask, entityByBodyId_, bodies_, *scene_);
            physicsSystem_.GetNarrowPhaseQuery().CollideShape(
                &queryShape, JPH::Vec3::sReplicate(1.0F), JPH::RMat44::sTranslation(ToJoltPosition(center)), settings, ToJoltPosition(center), collector, {}, {}, bodyFilter);
        });
    }

    void SynchronizeBody(
        SceneEntity entity,
        const TransformComponent& transform,
        const RigidbodyComponent& rigidbody,
        const ColliderComponent& collider,
        SceneSystemContext& context) {
        seenEntities_->insert(entity.Id());
        const BodySignature signature = MakeSignature(rigidbody, collider, transform);
        const auto existing = bodies_.find(entity.Id());
        if (existing != bodies_.end() && existing->second.signature == signature) {
            if (rigidbody.bodyType == RigidbodyBodyType::Kinematic &&
                existing->second.pendingKinematicMove) {
                existing->second.pendingKinematicMove = false;
                return;
            }
            SynchronizeKinematicOrStaticBody(existing->second.bodyId, rigidbody, collider, transform, context.DeltaSeconds());
            return;
        }

        if (existing != bodies_.end()) {
            RemoveBody(existing->second.bodyId);
            bodies_.erase(existing);
        }
        const JPH::BodyID bodyId = CreateBody(rigidbody, collider, transform);
        bodies_.emplace(entity.Id(), BodyRecord{ .bodyId = bodyId, .signature = signature });
        entityByBodyId_.emplace(bodyId, entity);
    }

private:
    [[nodiscard]] const JPH::BodyID* FindCurrentBodyId(SceneEntity entity) const noexcept {
        const auto existing = bodies_.find(entity.Id());
        if (existing == bodies_.end() || scene_ == nullptr || !scene_->Entities().IsAlive(entity)) {
            return nullptr;
        }
        const ColliderComponent* collider = scene_->Components().Colliders().TryGet(entity);
        const TransformComponent* transform = scene_->Transforms().TryGet(entity);
        if (collider == nullptr || transform == nullptr) {
            return nullptr;
        }
        const RigidbodyComponent* rigidbody = scene_->Components().Rigidbodies().TryGet(entity);
        RigidbodyComponent implicitStatic;
        implicitStatic.bodyType = RigidbodyBodyType::Static;
        return existing->second.signature == MakeSignature(rigidbody != nullptr ? *rigidbody : implicitStatic, *collider, *transform) ? &existing->second.bodyId : nullptr;
    }

    [[nodiscard]] const JPH::BodyID* FindBodyId(SceneEntity entity) const noexcept {
        const auto existing = bodies_.find(entity.Id());
        return existing == bodies_.end() ? nullptr : &existing->second.bodyId;
    }

    [[nodiscard]] const BodyRecord* FindDynamicBody(SceneEntity entity) const noexcept {
        return FindLiveBody(entity, RigidbodyBodyType::Dynamic);
    }

    [[nodiscard]] BodyRecord* FindKinematicBody(SceneEntity entity) noexcept {
        BodyRecord* body = FindLiveBody(entity, RigidbodyBodyType::Kinematic);
        return body;
    }

    [[nodiscard]] BodyRecord* FindLiveBody(SceneEntity entity, RigidbodyBodyType expectedType) noexcept {
        return const_cast<BodyRecord*>(std::as_const(*this).FindLiveBody(entity, expectedType));
    }

    [[nodiscard]] const BodyRecord* FindLiveBody(SceneEntity entity, RigidbodyBodyType expectedType) const noexcept {
        const auto existing = bodies_.find(entity.Id());
        if (existing == bodies_.end() || existing->second.signature.bodyType != expectedType || scene_ == nullptr ||
            !scene_->Entities().IsAlive(entity)) {
            return nullptr;
        }
        const RigidbodyComponent* rigidbody = scene_->Components().Rigidbodies().TryGet(entity);
        const ColliderComponent* collider = scene_->Components().Colliders().TryGet(entity);
        const TransformComponent* transform = scene_->Transforms().TryGet(entity);
        return rigidbody != nullptr && collider != nullptr && transform != nullptr && rigidbody->bodyType == expectedType &&
                existing->second.signature == MakeSignature(*rigidbody, *collider, *transform)
            ? &existing->second
            : nullptr;
    }

    [[nodiscard]] static std::uint32_t WorkerThreadCount() noexcept {
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        return hardwareThreads <= 1U ? 0U : static_cast<std::uint32_t>(hardwareThreads - 1U);
    }

    void SynchronizeBodies(SceneSystemContext& context) {
        physicsBodyScratch_.clear();
        physicsBodyScratch_.reserve(std::max<std::size_t>(bodies_.size(), 16U));
        constexpr kb::ecs::QueryExecutionSettings settings{
            .maxBatchSize = 1024U,
            .policy = kb::ecs::QueryExecutionPolicy::SingleThread,
        };
        std::unordered_set<std::uint64_t> rigidbodyEntities;
        {
            PhysicsBodyQuery physicsBodyQuery = context.EcsWorld().CreateQuery<TransformComponent, RigidbodyComponent, ColliderComponent>();
            physicsBodyQuery.ForEachBatchKernel(settings, [this, &rigidbodyEntities](const PhysicsBodyQuery::Batch& batch) {
                const TransformComponent* transforms = batch.Components<0>();
                const RigidbodyComponent* rigidbodies = batch.Components<1>();
                const ColliderComponent* colliders = batch.Components<2>();
                for (std::size_t index = 0; index < batch.Count(); ++index) {
                    const SceneEntity entity{ batch.EntityAt(index).Id() };
                    rigidbodyEntities.insert(entity.Id());
                    physicsBodyScratch_.push_back(PhysicsBodySnapshot{
                        .entity = entity,
                        .transform = transforms[index],
                        .rigidbody = rigidbodies[index],
                        .collider = colliders[index],
                    });
                }
            });
        }
        // Unity-like: a Collider without a Rigidbody becomes an implicit Static body.
        {
            ColliderOnlyBodyQuery colliderQuery = context.EcsWorld().CreateQuery<TransformComponent, ColliderComponent>();
            colliderQuery.ForEachBatchKernel(settings, [this, &rigidbodyEntities](const ColliderOnlyBodyQuery::Batch& batch) {
                const TransformComponent* transforms = batch.Components<0>();
                const ColliderComponent* colliders = batch.Components<1>();
                for (std::size_t index = 0; index < batch.Count(); ++index) {
                    const SceneEntity entity{ batch.EntityAt(index).Id() };
                    if (rigidbodyEntities.find(entity.Id()) != rigidbodyEntities.end()) {
                        continue; // already handled above with its explicit Rigidbody
                    }
                    physicsBodyScratch_.push_back(PhysicsBodySnapshot{
                        .entity = entity,
                        .transform = transforms[index],
                        .rigidbody = RigidbodyComponent{ .bodyType = RigidbodyBodyType::Static },
                        .collider = colliders[index],
                    });
                }
            });
        }

        std::unordered_set<std::uint64_t> seen;
        seen.reserve(std::max(bodies_.size(), physicsBodyScratch_.size()));
        seenEntities_ = &seen;

        for (const PhysicsBodySnapshot& body : physicsBodyScratch_) {
            SynchronizeBody(body.entity, body.transform, body.rigidbody, body.collider, context);
        }
        seenEntities_ = nullptr;

        for (auto it = bodies_.begin(); it != bodies_.end();) {
            if (seen.find(it->first) == seen.end()) {
                RemoveBody(it->second.bodyId);
                it = bodies_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = pendingRigidbodyRootMotion_.begin(); it != pendingRigidbodyRootMotion_.end();) {
            if (seen.find(it->first) == seen.end()) {
                it = pendingRigidbodyRootMotion_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // LIB-130: runs AFTER SynchronizeBodies (above) in the same OnFixedUpdate
    // - a joint's owner and (if set) connected entity must already have live
    // bodies this step before a constraint referencing them can be built.
    void SynchronizeJoints(SceneSystemContext& context) {
        jointScratch_.clear();
        jointScratch_.reserve(std::max<std::size_t>(joints_.size(), 4U));
        constexpr kb::ecs::QueryExecutionSettings settings{
            .maxBatchSize = 1024U,
            .policy = kb::ecs::QueryExecutionPolicy::SingleThread,
        };
        {
            JointQuery jointQuery = context.EcsWorld().CreateQuery<JointComponent>();
            jointQuery.ForEachBatchKernel(settings, [this](const JointQuery::Batch& batch) {
                const JointComponent* joints = batch.Components<0>();
                for (std::size_t index = 0; index < batch.Count(); ++index) {
                    jointScratch_.push_back(JointSnapshot{
                        .entity = SceneEntity{ batch.EntityAt(index).Id() },
                        .joint = joints[index],
                    });
                }
            });
        }

        std::unordered_set<std::uint64_t> seen;
        seen.reserve(std::max(joints_.size(), jointScratch_.size()));
        for (const JointSnapshot& snapshot : jointScratch_) {
            seen.insert(snapshot.entity.Id());
            SynchronizeJoint(snapshot.entity, snapshot.joint);
        }

        for (auto it = joints_.begin(); it != joints_.end();) {
            if (seen.find(it->first) == seen.end()) {
                RemoveJointRecord(it->second);
                it = joints_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // A joint that held a body motionless for long enough can put it to
    // sleep (JPH::BodyCreationSettings::mAllowSleeping defaults to true,
    // never overridden here) - removing or rebuilding its constraint
    // changes what (if anything) holds that body up, so both sides need
    // waking to actually respond to gravity/forces again instead of
    // sitting frozen in whatever pose they had when they fell asleep.
    void RemoveJointRecord(const JointRecord& record) {
        physicsSystem_.RemoveConstraint(record.constraint);
        JPH::BodyInterface& bodyInterface = physicsSystem_.GetBodyInterface();
        if (!record.signature.ownerBodyId.IsInvalid()) {
            bodyInterface.ActivateBody(record.signature.ownerBodyId);
        }
        if (!record.signature.connectedBodyId.IsInvalid()) {
            bodyInterface.ActivateBody(record.signature.connectedBodyId);
        }
    }

    // Honest skip (no constraint built/updated this step, retried the next)
    // when the owner or connected entity has no live body yet - mirrors
    // LIB-014's Physics.SetVelocity-in-Tick retry pattern for the exact same
    // underlying reason: a freshly spawned entity's Jolt body does not exist
    // until its first SynchronizeBody call.
    void SynchronizeJoint(SceneEntity entity, const JointComponent& joint) {
        const JPH::BodyID* ownerBodyId = FindBodyId(entity);
        if (ownerBodyId == nullptr) {
            return;
        }

        const bool connectedToWorld = !joint.connectedEntity.IsValid();
        JPH::BodyID connectedBodyId;
        if (!connectedToWorld) {
            const JPH::BodyID* found = FindBodyId(joint.connectedEntity);
            if (found == nullptr) {
                return;
            }
            connectedBodyId = *found;
        }

        const JointSignature signature = MakeJointSignature(joint, *ownerBodyId, connectedBodyId);
        const auto existing = joints_.find(entity.Id());
        if (existing != joints_.end() && existing->second.signature == signature) {
            return;
        }
        if (existing != joints_.end()) {
            RemoveJointRecord(existing->second);
            joints_.erase(existing);
        }

        JPH::Ref<JPH::Constraint> constraint;
        if (connectedToWorld) {
            const JPH::BodyID lockIds[1] = { *ownerBodyId };
            JPH::BodyLockMultiWrite lock(physicsSystem_.GetBodyLockInterface(), lockIds, 1);
            JPH::Body* body1 = lock.GetBody(0);
            if (body1 != nullptr) {
                constraint = CreateJointConstraint(joint, *body1, JPH::Body::sFixedToWorld);
            }
        } else {
            const JPH::BodyID lockIds[2] = { *ownerBodyId, connectedBodyId };
            JPH::BodyLockMultiWrite lock(physicsSystem_.GetBodyLockInterface(), lockIds, 2);
            JPH::Body* body1 = lock.GetBody(0);
            JPH::Body* body2 = lock.GetBody(1);
            if (body1 != nullptr && body2 != nullptr) {
                constraint = CreateJointConstraint(joint, *body1, *body2);
            }
        }
        if (constraint == nullptr) {
            return; // A body vanished between FindBodyId and the lock - honest skip, retried next step.
        }
        physicsSystem_.AddConstraint(constraint);
        joints_.emplace(entity.Id(), JointRecord{ .constraint = constraint, .signature = signature });
    }

    void RemoveAllJoints() {
        for (const auto& [entityId, record] : joints_) {
            static_cast<void>(entityId);
            physicsSystem_.RemoveConstraint(record.constraint);
        }
        joints_.clear();
    }

    [[nodiscard]] CharacterRecord* FindCharacterRecord(SceneEntity entity) noexcept {
        const auto existing = characters_.find(entity.Id());
        return existing == characters_.end() ? nullptr : &existing->second;
    }

    [[nodiscard]] const CharacterRecord* FindCharacterRecord(SceneEntity entity) const noexcept {
        const auto existing = characters_.find(entity.Id());
        return existing == characters_.end() ? nullptr : &existing->second;
    }

    // LIB-131: a CharacterControllerComponent entity is never a Rigidbody/Collider (see
    // CharacterSnapshot's own comment) - runs alongside SynchronizeBodies/SynchronizeJoints
    // in OnFixedUpdate, entirely independent of bodies_/joints_.
    void SynchronizeCharacters(SceneSystemContext& context) {
        characterScratch_.clear();
        characterScratch_.reserve(std::max<std::size_t>(characters_.size(), 4U));
        constexpr kb::ecs::QueryExecutionSettings settings{
            .maxBatchSize = 1024U,
            .policy = kb::ecs::QueryExecutionPolicy::SingleThread,
        };
        {
            CharacterQuery characterQuery = context.EcsWorld().CreateQuery<TransformComponent, CharacterControllerComponent>();
            characterQuery.ForEachBatchKernel(settings, [this](const CharacterQuery::Batch& batch) {
                const TransformComponent* transforms = batch.Components<0>();
                const CharacterControllerComponent* characterComponents = batch.Components<1>();
                for (std::size_t index = 0; index < batch.Count(); ++index) {
                    characterScratch_.push_back(CharacterSnapshot{
                        .entity = SceneEntity{ batch.EntityAt(index).Id() },
                        .transform = transforms[index],
                        .character = characterComponents[index],
                    });
                }
            });
        }

        std::unordered_set<std::uint64_t> seen;
        seen.reserve(std::max(characters_.size(), characterScratch_.size()));
        for (const CharacterSnapshot& snapshot : characterScratch_) {
            seen.insert(snapshot.entity.Id());
            SynchronizeCharacter(snapshot.entity, snapshot.transform, snapshot.character);
        }

        for (auto it = characters_.begin(); it != characters_.end();) {
            if (seen.find(it->first) == seen.end()) {
                it = characters_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = pendingCharacterRootMotion_.begin(); it != pendingCharacterRootMotion_.end();) {
            if (seen.find(it->first) == seen.end()) {
                it = pendingCharacterRootMotion_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Only a shape change (CharacterSignature) destroys+recreates the real JPH::CharacterVirtual;
    // slopeLimitDegrees/stepOffset/gravityScale/useGravity are refreshed on the record every
    // step regardless (slopeLimitDegrees applied live via SetMaxSlopeAngle - the other three
    // have no Jolt-side "live setter", they are read directly off CharacterRecord by
    // UpdateCharacters below instead).
    void SynchronizeCharacter(SceneEntity entity, const TransformComponent& transform, const CharacterControllerComponent& component) {
        const CharacterSignature signature = MakeCharacterSignature(component, transform);
        const auto existing = characters_.find(entity.Id());
        if (existing != characters_.end() && existing->second.signature == signature) {
            existing->second.character->SetMaxSlopeAngle(JPH::DegreesToRadians(component.slopeLimitDegrees));
            existing->second.stepOffset = component.stepOffset;
            existing->second.gravityScale = component.gravityScale;
            existing->second.useGravity = component.useGravity;
            return;
        }

        // A rebuild (shape changed, or first creation) starts fresh at the entity's CURRENT
        // transform - WriteBackCharacters keeps that transform continuously in sync with the
        // OLD character's own last position every prior step, so this is not a position
        // reset in practice, only real velocity carries over explicitly below.
        Vec3 moveInput{};
        float pendingJumpSpeed = 0.0F;
        if (existing != characters_.end()) {
            moveInput = existing->second.moveInput;
            pendingJumpSpeed = existing->second.pendingJumpSpeed;
            characters_.erase(existing);
        }

        const float scaleX = AbsScale(transform.worldScale.x);
        const float scaleY = AbsScale(transform.worldScale.y);
        const float scaleZ = AbsScale(transform.worldScale.z);

        JPH::CharacterVirtualSettings characterSettings;
        characterSettings.mShape = CreateCapsuleShape(component.radius * std::max(scaleX, scaleZ), component.height * scaleY);
        characterSettings.mShapeOffset = ToJolt(component.center);
        characterSettings.mMaxSlopeAngle = JPH::DegreesToRadians(component.slopeLimitDegrees);

        const JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
            &characterSettings, ToJoltPosition(transform.worldPosition), ToJolt(transform.worldRotation), &physicsSystem_);
        characters_.emplace(entity.Id(), CharacterRecord{
                                              .character = character,
                                              .signature = signature,
                                              .moveInput = moveInput,
                                              .pendingJumpSpeed = pendingJumpSpeed,
                                              .stepOffset = component.stepOffset,
                                              .gravityScale = component.gravityScale,
                                              .useGravity = component.useGravity,
                                          });
    }

    // LIB-131: the movement algorithm below is a faithful port of Jolt's OWN reference
    // implementation (third_party/jolt/Samples/Tests/Character/CharacterVirtualTest.cpp,
    // HandleInput) - not reinvented. Runs BEFORE Step() - see OnFixedUpdate's own comment.
    void UpdateCharacters(SceneSystemContext& context) {
        const float deltaSeconds = context.DeltaSeconds();
        if (deltaSeconds <= 0.0F) {
            return;
        }
        const JPH::ObjectLayer characterLayer = ToObjectLayer(0U, false);
        for (auto& [entityId, record] : characters_) {
            static_cast<void>(entityId);
            JPH::CharacterVirtual* character = record.character;

            // A cheaper re-estimate of ground velocity than a full contact refresh - the
            // ground body's own velocity may have changed since the last contact was
            // detected (e.g. SynchronizeBodies just re-derived a Kinematic platform's
            // velocity from its transform this very step).
            character->UpdateGroundVelocity();

            const JPH::Vec3 up = character->GetUp();
            const JPH::Vec3 currentVerticalVelocity = character->GetLinearVelocity().Dot(up) * up;
            const JPH::Vec3 groundVelocity = character->GetGroundVelocity();
            const bool movingTowardsGround = (currentVerticalVelocity.Dot(up) - groundVelocity.Dot(up)) < 0.1F;

            JPH::Vec3 newVelocity;
            if (character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround && movingTowardsGround) {
                // LIB-131 "platform motion": assume the velocity of whatever is stood on -
                // zero for static/no ground, the real moving velocity for a Kinematic/Dynamic
                // platform, so the character rides along with it automatically.
                newVelocity = groundVelocity;
                // LIB-131 jump: only actually honored while grounded and not already moving
                // away from the ground - a request made while airborne was already dropped by
                // this same `if` not being taken, so pendingJumpSpeed is simply never added.
                if (record.pendingJumpSpeed > 0.0F) {
                    newVelocity += up * record.pendingJumpSpeed;
                }
            } else {
                newVelocity = currentVerticalVelocity;
            }
            record.pendingJumpSpeed = 0.0F;

            // LIB-131 gravity: this component's own gravityScale/useGravity, not the scene
            // gravity vector unmodified - mirrors RigidbodyComponent's identically-named
            // fields, since a character has no Rigidbody of its own to read them from.
            const JPH::Vec3 gravity = physicsSystem_.GetGravity() * (record.useGravity ? record.gravityScale : 0.0F);
            newVelocity += gravity * deltaSeconds;

            // LIB-131 movement input (Physics.CharacterMove): horizontal only - any vertical
            // component is projected out so a script cannot bypass CharacterJump/gravity by
            // passing a nonzero Y through CharacterMove instead (ScriptPhysicsApi.cpp's
            // Physics.CharacterMove pins do not even accept a Y value).
            const JPH::Vec3 desiredVelocity = ToJolt(record.moveInput);
            newVelocity += desiredVelocity - desiredVelocity.Dot(up) * up;

            const auto rootMotion = pendingCharacterRootMotion_.find(entityId);
            if (rootMotion != pendingCharacterRootMotion_.end()) {
                const SceneEntity entity{ entityId };
                const kb::scene::Animator* animator =
                    context.GetScene().Components().Animators().TryGet(entity);
                if (animator == nullptr ||
                    !animator->enabled ||
                    !context.GetScene().Entities().IsActive(entity) ||
                    animator->rootMotionOwner !=
                        kb::scene::AnimatorRootMotionOwner::CharacterController ||
                    rootMotion->second.empty() ||
                    rootMotion->second.front().animatorRuntimeGeneration !=
                        context.GetScene().Animators().RuntimeBindingGeneration(entity) ||
                    context.GetScene().Components().Rigidbodies().TryGet(entity) != nullptr ||
                    context.GetScene().Components().Colliders().TryGet(entity) != nullptr) {
                    pendingCharacterRootMotion_.erase(rootMotion);
                } else {
                    const RootMotionSegment motion =
                        ConsumeRootMotion(rootMotion->second, deltaSeconds);
                    const Quat worldRotation = FromJolt(character->GetRotation());
                    const Vec3 worldTranslation =
                        kb::math::Rotate(worldRotation, motion.localTranslation);
                    newVelocity += ToJolt(worldTranslation * (1.0F / deltaSeconds));
                    character->SetRotation(ToJolt(kb::math::Normalize(
                        worldRotation * motion.localRotation)));
                    if (rootMotion->second.empty()) {
                        pendingCharacterRootMotion_.erase(rootMotion);
                    }
                }
            }

            character->SetLinearVelocity(newVelocity);

            // LIB-131 step offset: ExtendedUpdate's own WalkStairs pass, magnitude taken from
            // this character's configured stepOffset (not Jolt's built-in 0.4 default, though
            // that IS this field's own default value - see CharacterControllerComponent.hpp).
            JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
            updateSettings.mWalkStairsStepUp = up * record.stepOffset;

            character->ExtendedUpdate(
                deltaSeconds,
                gravity,
                updateSettings,
                physicsSystem_.GetDefaultBroadPhaseLayerFilter(characterLayer),
                physicsSystem_.GetDefaultLayerFilter(characterLayer),
                {},
                {},
                tempAllocator_);
        }
    }

    void WriteBackCharacters(SceneSystemContext& context) {
        for (const auto& [entityId, record] : characters_) {
            SceneEntity entity{ entityId };
            if (!context.Transforms().IsAlive(entity)) {
                continue;
            }
            TransformComponent* transform = context.Transforms().TryGet(entity);
            if (transform == nullptr) {
                continue;
            }
            const Vec3 position = FromJoltPosition(record.character->GetPosition());
            const Quat rotation = FromJolt(record.character->GetRotation());
            StageWriteBackWorldPose(*transform, position, rotation);
            writeBackEntities_.push_back(entity);
        }
    }

    void RemoveAllCharacters() {
        characters_.clear();
        pendingCharacterRootMotion_.clear();
    }

    [[nodiscard]] JPH::BodyID CreateBody(const RigidbodyComponent& rigidbody, const ColliderComponent& collider, const TransformComponent& transform) {
        JPH::RefConst<JPH::Shape> shape = CreateShape(collider, transform.worldScale);
        const Vec3 bodyPosition = Add(transform.worldPosition, ColliderWorldOffset(collider.center, transform.worldScale, transform.worldRotation));
        const std::uint32_t namedLayer = kb::scene::LowestSetPhysicsLayerIndex(collider.layer);
        const bool isStaticBody = rigidbody.bodyType == RigidbodyBodyType::Static;

        JPH::BodyCreationSettings bodySettings(shape, ToJoltPosition(bodyPosition), ToJolt(transform.worldRotation), ToMotionType(rigidbody.bodyType), ToObjectLayer(namedLayer, isStaticBody));
        bodySettings.mIsSensor = collider.trigger;
        bodySettings.mFriction = collider.friction;
        bodySettings.mRestitution = collider.restitution;
        bodySettings.mUserData = collider.layer;
        bodySettings.mLinearVelocity = ToJolt(rigidbody.linearVelocity);
        bodySettings.mAngularVelocity = ToJolt(rigidbody.angularVelocity);
        bodySettings.mGravityFactor = rigidbody.useGravity ? rigidbody.gravityScale : 0.0F;
        // LIB-133: fast mover / tunneling - Jolt's own default (Discrete) can tunnel a
        // fast-moving body clean through a thin collider within a single fixed step; LinearCast
        // sweeps the shape from start to destination instead.
        bodySettings.mMotionQuality = rigidbody.useContinuousCollision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        if (rigidbody.lockRotation) {
            bodySettings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
        }
        if (rigidbody.bodyType == RigidbodyBodyType::Dynamic) {
            bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bodySettings.mMassPropertiesOverride.mMass = ClampPositive(rigidbody.mass);
        }

        return physicsSystem_.GetBodyInterface().CreateAndAddBody(bodySettings, rigidbody.bodyType == RigidbodyBodyType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    }

    void SynchronizeKinematicOrStaticBody(JPH::BodyID bodyId, const RigidbodyComponent& rigidbody, const ColliderComponent& collider, const TransformComponent& transform, float fixedDeltaSeconds) {
        if (rigidbody.bodyType == RigidbodyBodyType::Dynamic) {
            return;
        }

        JPH::BodyInterface& bodyInterface = physicsSystem_.GetBodyInterface();
        const Vec3 bodyPosition = Add(transform.worldPosition, ColliderWorldOffset(collider.center, transform.worldScale, transform.worldRotation));
        if (rigidbody.bodyType == RigidbodyBodyType::Kinematic && fixedDeltaSeconds > 0.0F) {
            bodyInterface.MoveKinematic(bodyId, ToJoltPosition(bodyPosition), ToJolt(transform.worldRotation), fixedDeltaSeconds);
            return;
        }
        bodyInterface.SetPositionAndRotationWhenChanged(bodyId, ToJoltPosition(bodyPosition), ToJolt(transform.worldRotation), JPH::EActivation::DontActivate);
    }

    void Step(float fixedDeltaSeconds) {
        if (fixedDeltaSeconds <= 0.0F) {
            return;
        }
        physicsSystem_.Update(fixedDeltaSeconds, settings_.collisionSteps, &tempAllocator_, &jobSystem_);
    }

    void WriteBack(SceneSystemContext& context) {
        JPH::BodyInterface& bodyInterface = physicsSystem_.GetBodyInterface();
        for (const auto& [entityId, body] : bodies_) {
            SceneEntity entity{ entityId };
            if (!context.Transforms().IsAlive(entity)) {
                continue;
            }

            RigidbodyComponent* rigidbody = context.GetScene().Components().Rigidbodies().TryGet(entity);
            TransformComponent* transform = context.Transforms().TryGet(entity);
            if (rigidbody == nullptr || transform == nullptr || rigidbody->bodyType == RigidbodyBodyType::Static) {
                continue;
            }

            const Quat rotation = FromJolt(bodyInterface.GetRotation(body.bodyId));
            const Vec3 position = Subtract(FromJoltPosition(bodyInterface.GetPosition(body.bodyId)), ColliderWorldOffset(body.signature.center, body.signature.scale, rotation));
            StageWriteBackWorldPose(*transform, position, rotation);
            writeBackEntities_.push_back(entity);

            rigidbody->linearVelocity = FromJolt(bodyInterface.GetLinearVelocity(body.bodyId));
            rigidbody->angularVelocity = FromJolt(bodyInterface.GetAngularVelocity(body.bodyId));
            context.GetScene().Components().Rigidbodies().MarkModified(entity);
        }
    }

    void FinalizeWriteBackLocalPoses(SceneSystemContext& context) {
        // All non-static Jolt bodies and CharacterVirtual instances have staged their current
        // world poses before this compact scratch list is consumed. Local conversion is
        // therefore independent of bodies_/characters_ hash order, including dynamic
        // parent+child and mixed rigidbody/character hierarchies.
        for (const SceneEntity entity : writeBackEntities_) {
            if (!context.Transforms().IsAlive(entity)) {
                continue;
            }
            TransformComponent* transform = context.Transforms().TryGet(entity);
            if (transform != nullptr) {
                FinalizeWriteBackLocalPose(context, entity, *transform);
            }
        }
    }

    // LIB-127: drains the raw, thread-buffered contact events Step() above
    // just collected (safe now - this runs on the main thread, after
    // physicsSystem_.Update() has returned), reduces sub-shape callbacks to
    // authoritative BODY-pair transitions, resolves BodyID -> SceneEntity,
    // and queues two
    // kb::scene::PendingCollisionEvent per pair (one per side - Jolt's own
    // ContactListener doc: "Typically this is done on both objects involved
    // in a collision event") for kb::script::ScriptRuntimeSceneSystem to
    // drain and dispatch as real, entity-local ScriptEvents.
    void DispatchContactEvents(SceneSystemContext& context) {
        std::vector<RawContactEvent> drained = contactListener_.DrainAndClear();
        if (drained.empty()) {
            return;
        }

        // Grouping by canonical BodyID pair makes cross-pair callback order
        // independent of Jolt's worker scheduling. The reducer below
        // compares authoritative active sets at fixed-step boundaries and
        // selects their lowest SubShapeIDPair payload, so compound-shape
        // results do not depend on callback interleaving either.
        std::map<BodyContactKey, std::vector<RawContactEvent>> grouped;
        for (const RawContactEvent& contact : drained) {
            grouped[BodyContactKey{ .body1 = contact.body1, .body2 = contact.body2 }].push_back(contact);
        }

        kb::scene::Scene& scene = context.GetScene();
        for (const auto& [bodyPair, contacts] : grouped) {
            const auto entity1It = entityByBodyId_.find(bodyPair.body1);
            const auto entity2It = entityByBodyId_.find(bodyPair.body2);
            if (entity1It == entityByBodyId_.end() || entity2It == entityByBodyId_.end()) {
                continue; // One side already removed/unmapped this frame - honest skip, not a crash.
            }

            ActiveBodyContact& active = activeContacts_[bodyPair];
            struct Dispatch {
                kb::scene::PhysicsContactPhase phase = kb::scene::PhysicsContactPhase::Enter;
                ContactPayload payload{};
            };
            std::vector<Dispatch> dispatches;
            const bool wasActive = !active.subShapes.empty();
            const ContactPayload previousPayload = wasActive ? active.subShapes.begin()->second : ContactPayload{};
            std::map<JPH::SubShapeIDPair, ContactPayload> activatedThisStep;

            for (const RawContactEvent& contact : contacts) {
                if (contact.phase == RawContactPhase::Removed) {
                    const auto subShapeIt = active.subShapes.find(contact.pair);
                    if (subShapeIt == active.subShapes.end()) {
                        // A stale Removed can arrive after an explicit body
                        // teardown discarded the authoritative state.
                        continue;
                    }
                    active.subShapes.erase(subShapeIt);
                } else {
                    const ContactPayload payload{
                        .point = contact.point,
                        .normal = contact.normal,
                        .isTrigger = contact.isSensor1 || contact.isSensor2,
                    };
                    active.subShapes[contact.pair] = payload;
                    activatedThisStep[contact.pair] = payload;
                }
            }

            const bool isActive = !active.subShapes.empty();
            if (!wasActive && isActive) {
                // Use the lowest SubShapeIDPair as representative once
                // more than one became active. Its operator< is Jolt's own
                // deterministic contact ordering.
                dispatches.push_back(Dispatch{
                    .phase = kb::scene::PhysicsContactPhase::Enter,
                    .payload = active.subShapes.begin()->second,
                });
            } else if (wasActive && isActive) {
                // The body pair existed at both fixed-step boundaries:
                // exactly one Stay, even when Jolt replaced individual
                // compound-shape manifolds during the step.
                dispatches.push_back(Dispatch{
                    .phase = kb::scene::PhysicsContactPhase::Stay,
                    .payload = active.subShapes.begin()->second,
                });
            } else if (wasActive) {
                // Removed has no manifold. The lowest previously-active
                // SubShapeIDPair is the stable retained payload; choosing
                // "whichever removal callback arrived last" would make
                // Exit depend on worker scheduling for compound shapes.
                dispatches.push_back(Dispatch{
                    .phase = kb::scene::PhysicsContactPhase::Exit,
                    .payload = previousPayload,
                });
            } else if (!activatedThisStep.empty()) {
                // A contact can begin and end within one physics update.
                // Preserve both observable transitions with the same
                // canonical payload rather than dropping the interaction.
                const ContactPayload transientPayload = activatedThisStep.begin()->second;
                dispatches.push_back(Dispatch{
                    .phase = kb::scene::PhysicsContactPhase::Enter,
                    .payload = transientPayload,
                });
                dispatches.push_back(Dispatch{
                    .phase = kb::scene::PhysicsContactPhase::Exit,
                    .payload = transientPayload,
                });
            }

            for (const Dispatch& dispatch : dispatches) {
                // `normal` is recipient-local: Jolt defines the manifold
                // normal from body1 toward body2, therefore body2 receives
                // its exact inverse. Point remains a world-space point for
                // both recipients.
                kb::scene::PhysicsBackend::QueueCollisionEvent(scene, kb::scene::PendingCollisionEvent{
                                                                           .target = entity1It->second,
                                                                           .other = entity2It->second,
                                                                           .point = dispatch.payload.point,
                                                                           .normal = dispatch.payload.normal,
                                                                           .isTrigger = dispatch.payload.isTrigger,
                                                                           .phase = dispatch.phase,
                                                                       });
                kb::scene::PhysicsBackend::QueueCollisionEvent(scene, kb::scene::PendingCollisionEvent{
                                                                           .target = entity2It->second,
                                                                           .other = entity1It->second,
                                                                           .point = dispatch.payload.point,
                                                                           .normal = Vec3{
                                                                               -dispatch.payload.normal.x,
                                                                               -dispatch.payload.normal.y,
                                                                               -dispatch.payload.normal.z,
                                                                           },
                                                                           .isTrigger = dispatch.payload.isTrigger,
                                                                           .phase = dispatch.phase,
                                                                       });
            }

            if (active.subShapes.empty()) {
                activeContacts_.erase(bodyPair);
            }
        }
    }

    void RemoveBody(JPH::BodyID bodyId) {
        if (bodyId.IsInvalid()) {
            return;
        }
        // LIB-130: a joint's constraint references this body internally (as
        // either the owner's or the connected entity's body) - remove any
        // such constraint FIRST, so Jolt never holds a constraint pointing
        // at a body that is about to be destroyed (this runs for BOTH the
        // "entity lost its Rigidbody/Collider" tail-removal path AND the
        // "signature changed, rebuild the body" path in SynchronizeBody, so
        // it must live here rather than in any one caller). SynchronizeJoints
        // (which always runs right after SynchronizeBodies in
        // OnFixedUpdate) rebuilds the constraint fresh once/if the
        // referencing body exists again.
        for (auto it = joints_.begin(); it != joints_.end();) {
            if (it->second.signature.ownerBodyId == bodyId || it->second.signature.connectedBodyId == bodyId) {
                RemoveJointRecord(it->second);
                it = joints_.erase(it);
            } else {
                ++it;
            }
        }
        JPH::BodyInterface& bodyInterface = physicsSystem_.GetBodyInterface();
        contactListener_.DiscardBody(bodyId);
        for (auto it = activeContacts_.begin(); it != activeContacts_.end();) {
            if (it->first.body1 == bodyId || it->first.body2 == bodyId) {
                it = activeContacts_.erase(it);
            } else {
                ++it;
            }
        }
        bodyInterface.RemoveBody(bodyId);
        bodyInterface.DestroyBody(bodyId);
        entityByBodyId_.erase(bodyId);
    }

    void RemoveAllBodies() {
        for (const auto& [entityId, body] : bodies_) {
            static_cast<void>(entityId);
            RemoveBody(body.bodyId);
        }
        bodies_.clear();
        pendingRigidbodyRootMotion_.clear();
    }

    JoltRuntime runtime_;
    JoltPhysicsSceneSystemSettings settings_;
    JPH::BroadPhaseLayerInterfaceTable broadPhaseLayers_;
    JPH::ObjectLayerPairFilterTable objectLayerPairFilter_;
    ObjectVsBroadPhaseLayerFilter objectVsBroadPhaseFilter_;
    JPH::PhysicsSystem physicsSystem_;
    JPH::TempAllocatorImpl tempAllocator_;
    JPH::JobSystemThreadPool jobSystem_;
    std::unordered_map<std::uint64_t, BodyRecord> bodies_;
    std::unordered_map<std::uint64_t, RootMotionQueue> pendingRigidbodyRootMotion_;
    kb::scene::Scene* scene_ = nullptr;
    std::unordered_map<JPH::BodyID, SceneEntity> entityByBodyId_;
    std::vector<PhysicsBodySnapshot> physicsBodyScratch_;
    std::unordered_set<std::uint64_t>* seenEntities_ = nullptr;
    std::unordered_map<std::uint64_t, JointRecord> joints_;
    std::vector<JointSnapshot> jointScratch_;
    std::unordered_map<std::uint64_t, CharacterRecord> characters_;
    std::unordered_map<std::uint64_t, RootMotionQueue> pendingCharacterRootMotion_;
    std::vector<CharacterSnapshot> characterScratch_;
    std::vector<SceneEntity> writeBackEntities_;
    JoltCollisionContactListener contactListener_;
    // LIB-127: authoritative active sub-shape sets, aggregated by body pair.
    // Besides preventing duplicate entity callbacks for compound shapes,
    // this retains the last manifold payload needed by OnContactRemoved.
    std::map<BodyContactKey, ActiveBodyContact> activeContacts_;
};

JoltPhysicsSceneSystem::JoltPhysicsSceneSystem()
    : JoltPhysicsSceneSystem(JoltPhysicsSceneSystemSettings{}) {}

JoltPhysicsSceneSystem::JoltPhysicsSceneSystem(JoltPhysicsSceneSystemSettings settings)
    : impl_(std::make_unique<Impl>(settings)) {}

JoltPhysicsSceneSystem::~JoltPhysicsSceneSystem() = default;

JoltPhysicsSceneSystem::JoltPhysicsSceneSystem(JoltPhysicsSceneSystem&&) noexcept = default;

JoltPhysicsSceneSystem& JoltPhysicsSceneSystem::operator=(JoltPhysicsSceneSystem&&) noexcept = default;

void JoltPhysicsSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    impl_->AttachScene(context.GetScene());
    kb::scene::PhysicsBackend::RegisterBackend(context.GetScene(), *impl_);
}

void JoltPhysicsSceneSystem::OnFixedUpdate(kb::scene::SceneSystemContext& context) {
    impl_->OnFixedUpdate(context);
}

void JoltPhysicsSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    kb::scene::PhysicsBackend::UnregisterBackend(context.GetScene(), *impl_);
    impl_->DetachScene();
    impl_->OnDestroy();
}

} // namespace kb::physics_jolt
