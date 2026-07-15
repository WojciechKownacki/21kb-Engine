#include "JoltPhysicsSceneSystem.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/World.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/PhysicsLayersAsset.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/scene/SceneSystemTransformAccess.hpp"

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
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

JPH_SUPPRESS_WARNINGS

namespace kb::physics_jolt {

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
    bool trigger = false;
    float friction = 0.5F;
    float restitution = 0.0F;
    std::uint32_t layer = 0x7FFFFFFFU;
};

struct BodyRecord {
    JPH::BodyID bodyId{};
    BodySignature signature{};
};

[[nodiscard]] float ClampPositive(float value) noexcept {
    return std::max(value, MinimumShapeExtent);
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

[[nodiscard]] bool SameVec3(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
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
        lhs.useGravity == rhs.useGravity && lhs.lockRotation == rhs.lockRotation && lhs.trigger == rhs.trigger &&
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

[[nodiscard]] JPH::RefConst<JPH::Shape> CreateShape(const ColliderComponent& collider, Vec3 scale) {
    const float scaleX = AbsScale(scale.x);
    const float scaleY = AbsScale(scale.y);
    const float scaleZ = AbsScale(scale.z);

    switch (collider.shape) {
    case ColliderShape::Sphere:
        return new JPH::SphereShape(ClampPositive(collider.radius * std::max({ scaleX, scaleY, scaleZ })));
    case ColliderShape::Capsule: {
        const float radius = ClampPositive(collider.radius * std::max(scaleX, scaleZ));
        const float scaledHeight = ClampPositive(collider.height * scaleY);
        const float halfCylinder = std::max(0.0F, (scaledHeight * 0.5F) - radius);
        return new JPH::CapsuleShape(halfCylinder, radius);
    }
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

struct JointSnapshot {
    SceneEntity entity{};
    JointComponent joint{};
};

using JointQuery = kb::ecs::Query<JointComponent>;

// LIB-125: builds a throwaway query shape (no Body/BodyID involved - Jolt
// supports constructing a Shape purely to pass to CastShape/CollideShape,
// the same construction calls CreateShape above already uses for real
// bodies) from the engine-facing, script-boundary-friendly PhysicsShapeDesc.
[[nodiscard]] JPH::RefConst<JPH::Shape> CreateQueryShape(const kb::scene::PhysicsShapeDesc& shape) {
    switch (shape.kind) {
    case kb::scene::PhysicsShapeKind::Sphere:
        return new JPH::SphereShape(ClampPositive(shape.radius));
    case kb::scene::PhysicsShapeKind::Capsule: {
        const float radius = ClampPositive(shape.radius);
        const float halfCylinder = std::max(0.0F, (shape.height * 0.5F) - radius);
        return new JPH::CapsuleShape(halfCylinder, radius);
    }
    case kb::scene::PhysicsShapeKind::Box:
        return new JPH::BoxShape(JPH::Vec3(
            ClampPositive(shape.boxHalfExtents.x),
            ClampPositive(shape.boxHalfExtents.y),
            ClampPositive(shape.boxHalfExtents.z)));
    }
    return new JPH::SphereShape(0.5F);
}

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
    explicit LayerMaskBodyFilter(std::uint32_t mask) noexcept
        : mask_(mask) {}

    [[nodiscard]] bool ShouldCollideLocked(const JPH::Body& body) const override {
        return (static_cast<std::uint32_t>(body.GetUserData()) & mask_) != 0U;
    }

private:
    std::uint32_t mask_ = 0U;
};

// ClosestPoint (LIB-125) targets ONE already-known entity's body specifically
// (not a broad-phase search), so its query only ever accepts that single body.
class SingleBodyOnlyFilter final : public JPH::BodyFilter {
public:
    explicit SingleBodyOnlyFilter(JPH::BodyID bodyId) noexcept
        : bodyId_(bodyId) {}

    [[nodiscard]] bool ShouldCollide(const JPH::BodyID& bodyId) const override {
        return bodyId == bodyId_;
    }

private:
    JPH::BodyID bodyId_;
};

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
        RemoveAllJoints();
        RemoveAllBodies();
    }

    void OnFixedUpdate(SceneSystemContext& context) {
        SynchronizeBodies(context);
        SynchronizeJoints(context);
        Step(context.DeltaSeconds());
        WriteBack(context);
        DispatchContactEvents(context);
    }

    void OnDestroy() {
        // LIB-130: a constraint references its two bodies internally -
        // remove joints BEFORE the bodies they connect, matching the real
        // dependency order (mirrors why Jolt itself requires
        // RemoveConstraint before the bodies it references are destroyed).
        RemoveAllJoints();
        RemoveAllBodies();
    }

    // kb::scene::IPhysicsBackend - LIB-124. Every method looks the entity up
    // in the SAME bodies_ map SynchronizeBodies already maintains; a miss
    // (no live body this frame - not yet synchronized, or the entity has no
    // Rigidbody/Collider at all) is a real, honest "not applied" (false /
    // found=false), never a crash or silent success.
    bool AddForce(SceneEntity entity, Vec3 force) noexcept override {
        const JPH::BodyID* bodyId = FindBodyId(entity);
        if (bodyId == nullptr) {
            return false;
        }
        physicsSystem_.GetBodyInterface().AddForce(*bodyId, ToJolt(force));
        return true;
    }

    bool AddImpulse(SceneEntity entity, Vec3 impulse) noexcept override {
        const JPH::BodyID* bodyId = FindBodyId(entity);
        if (bodyId == nullptr) {
            return false;
        }
        physicsSystem_.GetBodyInterface().AddImpulse(*bodyId, ToJolt(impulse));
        return true;
    }

    bool SetVelocity(SceneEntity entity, Vec3 velocity) noexcept override {
        const JPH::BodyID* bodyId = FindBodyId(entity);
        if (bodyId == nullptr) {
            return false;
        }
        physicsSystem_.GetBodyInterface().SetLinearVelocity(*bodyId, ToJolt(velocity));
        return true;
    }

    [[nodiscard]] kb::scene::PhysicsVectorResult GetVelocity(SceneEntity entity) const noexcept override {
        const JPH::BodyID* bodyId = FindBodyId(entity);
        if (bodyId == nullptr) {
            return {};
        }
        return kb::scene::PhysicsVectorResult{ .found = true, .value = FromJolt(physicsSystem_.GetBodyInterface().GetLinearVelocity(*bodyId)) };
    }

    bool SetAngularVelocity(SceneEntity entity, Vec3 angularVelocity) noexcept override {
        const JPH::BodyID* bodyId = FindBodyId(entity);
        if (bodyId == nullptr) {
            return false;
        }
        physicsSystem_.GetBodyInterface().SetAngularVelocity(*bodyId, ToJolt(angularVelocity));
        return true;
    }

    [[nodiscard]] kb::scene::PhysicsVectorResult GetAngularVelocity(SceneEntity entity) const noexcept override {
        const JPH::BodyID* bodyId = FindBodyId(entity);
        if (bodyId == nullptr) {
            return {};
        }
        return kb::scene::PhysicsVectorResult{ .found = true, .value = FromJolt(physicsSystem_.GetBodyInterface().GetAngularVelocity(*bodyId)) };
    }

    bool MoveKinematic(SceneEntity entity, Vec3 targetPosition, Quat targetRotation, float deltaSeconds) noexcept override {
        const auto existing = bodies_.find(entity.Id());
        if (existing == bodies_.end() || existing->second.signature.bodyType != RigidbodyBodyType::Kinematic || deltaSeconds <= 0.0F) {
            return false;
        }
        physicsSystem_.GetBodyInterface().MoveKinematic(existing->second.bodyId, ToJoltPosition(targetPosition), ToJolt(targetRotation), deltaSeconds);
        return true;
    }

    bool Sleep(SceneEntity entity) noexcept override {
        const JPH::BodyID* bodyId = FindBodyId(entity);
        if (bodyId == nullptr) {
            return false;
        }
        physicsSystem_.GetBodyInterface().DeactivateBody(*bodyId);
        return true;
    }

    bool Wake(SceneEntity entity) noexcept override {
        const JPH::BodyID* bodyId = FindBodyId(entity);
        if (bodyId == nullptr) {
            return false;
        }
        physicsSystem_.GetBodyInterface().ActivateBody(*bodyId);
        return true;
    }

    [[nodiscard]] bool IsSleeping(SceneEntity entity) const noexcept override {
        const JPH::BodyID* bodyId = FindBodyId(entity);
        return bodyId != nullptr && !physicsSystem_.GetBodyInterface().IsActive(*bodyId);
    }

    [[nodiscard]] kb::scene::PhysicsCastResult CastShape(const kb::scene::PhysicsShapeDesc& shape, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask) const noexcept override {
        const JPH::Vec3 joltDirection = ToJolt(direction);
        const float directionLength = joltDirection.Length();
        if (maxDistance <= 0.0F || directionLength <= 0.000001F) {
            return {};
        }
        const JPH::RefConst<JPH::Shape> queryShape = CreateQueryShape(shape);
        const JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
            queryShape, JPH::Vec3::sReplicate(1.0F), JPH::RMat44::sTranslation(ToJoltPosition(origin)), (joltDirection / directionLength) * maxDistance);
        JPH::ShapeCastSettings settings;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        const LayerMaskBodyFilter bodyFilter(layerMask);
        physicsSystem_.GetNarrowPhaseQuery().CastShape(shapeCast, settings, ToJoltPosition(origin), collector, {}, {}, bodyFilter);
        if (!collector.HadHit()) {
            return {};
        }
        const auto entityIt = entityByBodyId_.find(collector.mHit.mBodyID2);
        if (entityIt == entityByBodyId_.end()) {
            return {};
        }
        // mContactPointOn2 is returned RELATIVE to inBaseOffset (origin, as
        // passed to CastShape above), not an absolute world position - add
        // it back to get the real hit point.
        return kb::scene::PhysicsCastResult{
            .hit = true,
            .entity = entityIt->second,
            .distance = collector.mHit.mFraction * maxDistance,
            .point = Add(origin, FromJolt(collector.mHit.mContactPointOn2)),
            .normal = FromJolt(-collector.mHit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero())),
        };
    }

    [[nodiscard]] kb::scene::PhysicsOverlapResult OverlapShape(const kb::scene::PhysicsShapeDesc& shape, Vec3 center, std::uint32_t layerMask) const noexcept override {
        const JPH::RefConst<JPH::Shape> queryShape = CreateQueryShape(shape);
        JPH::CollideShapeSettings settings;
        JPH::ClosestHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const LayerMaskBodyFilter bodyFilter(layerMask);
        physicsSystem_.GetNarrowPhaseQuery().CollideShape(
            queryShape, JPH::Vec3::sReplicate(1.0F), JPH::RMat44::sTranslation(ToJoltPosition(center)), settings, ToJoltPosition(center), collector, {}, {}, bodyFilter);
        if (!collector.HadHit()) {
            return {};
        }
        const auto entityIt = entityByBodyId_.find(collector.mHit.mBodyID2);
        if (entityIt == entityByBodyId_.end()) {
            return {};
        }
        return kb::scene::PhysicsOverlapResult{ .overlapping = true, .entity = entityIt->second };
    }

    [[nodiscard]] kb::scene::PhysicsClosestPointResult ClosestPoint(SceneEntity entity, Vec3 point) const noexcept override {
        const JPH::BodyID* bodyId = FindBodyId(entity);
        if (bodyId == nullptr) {
            return {};
        }
        // A tiny point-like sphere at the query point, restricted to collide
        // ONLY with the target body (SingleBodyOnlyFilter) - with
        // mMaxSeparationDistance set large, CollideShapeResult::
        // mContactPointOn2 gives the closest point on the target's real
        // surface regardless of how far away it is, and mPenetrationDepth
        // (negative when separated) gives the distance - the documented
        // Jolt pattern for closest-point queries (no dedicated API exists).
        constexpr float PointRadius = 0.01F;
        constexpr float MaxSearchDistance = 1.0e6F;
        const JPH::SphereShape pointShape(PointRadius);
        JPH::CollideShapeSettings settings;
        settings.mMaxSeparationDistance = MaxSearchDistance;
        JPH::ClosestHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const SingleBodyOnlyFilter bodyFilter(*bodyId);
        physicsSystem_.GetNarrowPhaseQuery().CollideShape(
            &pointShape, JPH::Vec3::sReplicate(1.0F), JPH::RMat44::sTranslation(ToJoltPosition(point)), settings, ToJoltPosition(point), collector, {}, {}, bodyFilter);
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
    // EVERY hit (JPH::AllHitCollisionCollector, not ClosestHitCollisionCollector)
    // into the caller's kb::library::ArrayNonAlloc buffer instead of only
    // the closest one - this implementation owns clearing/filling `results`
    // completely (PhysicsBackend::CastShapeAll only calls this when a
    // backend IS registered; the no-backend clear lives there instead).
    void CastShapeAll(const kb::scene::PhysicsShapeDesc& shape, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask, kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult>& results) const noexcept override {
        results.Clear();
        const JPH::Vec3 joltDirection = ToJolt(direction);
        const float directionLength = joltDirection.Length();
        if (maxDistance <= 0.0F || directionLength <= 0.000001F) {
            return;
        }
        const JPH::RefConst<JPH::Shape> queryShape = CreateQueryShape(shape);
        const JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
            queryShape, JPH::Vec3::sReplicate(1.0F), JPH::RMat44::sTranslation(ToJoltPosition(origin)), (joltDirection / directionLength) * maxDistance);
        JPH::ShapeCastSettings settings;
        JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
        const LayerMaskBodyFilter bodyFilter(layerMask);
        physicsSystem_.GetNarrowPhaseQuery().CastShape(shapeCast, settings, ToJoltPosition(origin), collector, {}, {}, bodyFilter);
        collector.Sort();
        for (const JPH::CastShapeCollector::ResultType& hit : collector.mHits) {
            const auto entityIt = entityByBodyId_.find(hit.mBodyID2);
            if (entityIt == entityByBodyId_.end()) {
                continue;
            }
            // mContactPointOn2 is relative to inBaseOffset (origin) - see
            // CastShape above for the same note.
            if (!results.PushBack(kb::scene::PhysicsCastResult{
                    .hit = true,
                    .entity = entityIt->second,
                    .distance = hit.mFraction * maxDistance,
                    .point = Add(origin, FromJolt(hit.mContactPointOn2)),
                    .normal = FromJolt(-hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero())),
                })) {
                break; // Buffer full; remaining hits are farther (already sorted), so nothing more would fit anyway.
            }
        }
    }

    void OverlapShapeAll(const kb::scene::PhysicsShapeDesc& shape, Vec3 center, std::uint32_t layerMask, kb::library::ArrayNonAlloc<kb::scene::PhysicsOverlapResult>& results) const noexcept override {
        results.Clear();
        const JPH::RefConst<JPH::Shape> queryShape = CreateQueryShape(shape);
        JPH::CollideShapeSettings settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const LayerMaskBodyFilter bodyFilter(layerMask);
        physicsSystem_.GetNarrowPhaseQuery().CollideShape(
            queryShape, JPH::Vec3::sReplicate(1.0F), JPH::RMat44::sTranslation(ToJoltPosition(center)), settings, ToJoltPosition(center), collector, {}, {}, bodyFilter);
        collector.Sort();
        for (const JPH::CollideShapeCollector::ResultType& hit : collector.mHits) {
            const auto entityIt = entityByBodyId_.find(hit.mBodyID2);
            if (entityIt == entityByBodyId_.end()) {
                continue;
            }
            if (!results.PushBack(kb::scene::PhysicsOverlapResult{ .overlapping = true, .entity = entityIt->second })) {
                break;
            }
        }
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
    [[nodiscard]] const JPH::BodyID* FindBodyId(SceneEntity entity) const noexcept {
        const auto existing = bodies_.find(entity.Id());
        return existing == bodies_.end() ? nullptr : &existing->second.bodyId;
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
        {
            PhysicsBodyQuery physicsBodyQuery = context.EcsWorld().CreateQuery<TransformComponent, RigidbodyComponent, ColliderComponent>();
            physicsBodyQuery.ForEachBatchKernel(settings, [this](const PhysicsBodyQuery::Batch& batch) {
                const TransformComponent* transforms = batch.Components<0>();
                const RigidbodyComponent* rigidbodies = batch.Components<1>();
                const ColliderComponent* colliders = batch.Components<2>();
                for (std::size_t index = 0; index < batch.Count(); ++index) {
                    physicsBodyScratch_.push_back(PhysicsBodySnapshot{
                        .entity = SceneEntity{ batch.EntityAt(index).Id() },
                        .transform = transforms[index],
                        .rigidbody = rigidbodies[index],
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

    [[nodiscard]] JPH::BodyID CreateBody(const RigidbodyComponent& rigidbody, const ColliderComponent& collider, const TransformComponent& transform) {
        JPH::RefConst<JPH::Shape> shape = CreateShape(collider, transform.worldScale);
        const Vec3 bodyPosition = Add(transform.worldPosition, collider.center);
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
        const Vec3 bodyPosition = Add(transform.worldPosition, collider.center);
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

            const Vec3 position = Subtract(FromJoltPosition(bodyInterface.GetPosition(body.bodyId)), body.signature.center);
            transform->localPosition = position;
            transform->worldPosition = position;
            transform->localRotation = FromJolt(bodyInterface.GetRotation(body.bodyId));
            transform->worldRotation = transform->localRotation;
            transform->worldDirty = true;
            context.Transforms().MarkModified(entity);

            rigidbody->linearVelocity = FromJolt(bodyInterface.GetLinearVelocity(body.bodyId));
            rigidbody->angularVelocity = FromJolt(bodyInterface.GetAngularVelocity(body.bodyId));
            context.GetScene().Components().Rigidbodies().MarkModified(entity);
        }
    }

    // LIB-127: drains the raw, thread-buffered contact events Step() above
    // just collected (safe now - this runs on the main thread, after
    // physicsSystem_.Update() has returned), reduces them to ONE
    // authoritative phase per (body1,body2) contact pair for this fixed
    // step, resolves BodyID -> SceneEntity, and queues two
    // kb::scene::PendingCollisionEvent per pair (one per side - Jolt's own
    // ContactListener doc: "Typically this is done on both objects involved
    // in a collision event") for kb::script::ScriptRuntimeSceneSystem to
    // drain and dispatch as real, entity-local ScriptEvents.
    void DispatchContactEvents(SceneSystemContext& context) {
        std::vector<RawContactEvent> drained = contactListener_.DrainAndClear();
        if (drained.empty()) {
            return;
        }

        // std::map, keyed by JPH::SubShapeIDPair - Jolt documents that
        // type's operator< as existing specifically "to consistently order
        // contact points for a deterministic simulation". Reusing it here
        // gives BOTH the per-step reduction below (Jolt may run several
        // internal collision sub-steps within one Step() call and report
        // the same pair more than once: Removed always wins as the step's
        // final word, Added otherwise wins over Persisted so a genuinely
        // new contact this step is never downgraded to a mere Stay) AND a
        // canonical, thread-schedule-independent dispatch order, from a
        // single data structure.
        std::map<JPH::SubShapeIDPair, RawContactEvent> reduced;
        for (const RawContactEvent& contact : drained) {
            const auto [it, inserted] = reduced.try_emplace(contact.pair, contact);
            if (inserted) {
                continue;
            }
            const bool shouldReplace = contact.phase == RawContactPhase::Removed ||
                (contact.phase == RawContactPhase::Added && it->second.phase != RawContactPhase::Removed);
            if (shouldReplace) {
                it->second = contact;
            }
        }

        kb::scene::Scene& scene = context.GetScene();
        for (const auto& [pair, contact] : reduced) {
            const auto entity1It = entityByBodyId_.find(contact.body1);
            const auto entity2It = entityByBodyId_.find(contact.body2);
            if (entity1It == entityByBodyId_.end() || entity2It == entityByBodyId_.end()) {
                continue; // One side already removed/unmapped this frame - honest skip, not a crash.
            }
            // Jolt's OnContactRemoved (unlike OnContactAdded/Persisted) gives
            // no Body access at all, so contact.isSensor1/2 are unset
            // (default false) for a Removed event - using them directly
            // here would silently misreport every trigger's Exit as a
            // non-trigger OnCollisionExit. pairIsTrigger_ remembers the
            // real answer from the pair's own Added/Persisted callbacks
            // (this SAME fixed step or an earlier one) and is consulted
            // (then erased - the pair is gone) on Removed instead.
            bool isTrigger = false;
            if (contact.phase == RawContactPhase::Removed) {
                const auto triggerIt = pairIsTrigger_.find(pair);
                if (triggerIt != pairIsTrigger_.end()) {
                    isTrigger = triggerIt->second;
                    pairIsTrigger_.erase(triggerIt);
                }
            } else {
                isTrigger = contact.isSensor1 || contact.isSensor2;
                pairIsTrigger_[pair] = isTrigger;
            }
            const kb::scene::PhysicsContactPhase phase = contact.phase == RawContactPhase::Added ? kb::scene::PhysicsContactPhase::Enter
                : contact.phase == RawContactPhase::Removed                                       ? kb::scene::PhysicsContactPhase::Exit
                                                                                                    : kb::scene::PhysicsContactPhase::Stay;
            // Body1/Body2 order is already canonical (Jolt guarantees body1
            // ID < body2 ID for Added/Persisted; SubShapeIDPair ordering
            // gives the same determinism for Removed), so target=entity1 is
            // always queued before target=entity2 for a given pair. Both
            // sides receive the SAME raw point/normal (Jolt's own
            // body1-relative convention) rather than a per-recipient
            // flipped normal - a deliberate, documented simplification, not
            // an oversight: nothing in the plan names per-side normal
            // flipping, and it is trivially derivable by the receiving
            // script (negate normal) if a specific use case needs it.
            kb::scene::PhysicsBackend::QueueCollisionEvent(scene, kb::scene::PendingCollisionEvent{
                                                                       .target = entity1It->second,
                                                                       .other = entity2It->second,
                                                                       .point = contact.point,
                                                                       .normal = contact.normal,
                                                                       .isTrigger = isTrigger,
                                                                       .phase = phase,
                                                                   });
            kb::scene::PhysicsBackend::QueueCollisionEvent(scene, kb::scene::PendingCollisionEvent{
                                                                       .target = entity2It->second,
                                                                       .other = entity1It->second,
                                                                       .point = contact.point,
                                                                       .normal = contact.normal,
                                                                       .isTrigger = isTrigger,
                                                                       .phase = phase,
                                                                   });
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
    std::unordered_map<JPH::BodyID, SceneEntity> entityByBodyId_;
    std::vector<PhysicsBodySnapshot> physicsBodyScratch_;
    std::unordered_set<std::uint64_t>* seenEntities_ = nullptr;
    std::unordered_map<std::uint64_t, JointRecord> joints_;
    std::vector<JointSnapshot> jointScratch_;
    JoltCollisionContactListener contactListener_;
    // LIB-127: which currently-active contact pairs are trigger contacts -
    // see DispatchContactEvents' own comment on why OnContactRemoved needs
    // this remembered rather than read directly off the (unavailable) body.
    std::map<JPH::SubShapeIDPair, bool> pairIsTrigger_;
};

JoltPhysicsSceneSystem::JoltPhysicsSceneSystem()
    : JoltPhysicsSceneSystem(JoltPhysicsSceneSystemSettings{}) {}

JoltPhysicsSceneSystem::JoltPhysicsSceneSystem(JoltPhysicsSceneSystemSettings settings)
    : impl_(std::make_unique<Impl>(settings)) {}

JoltPhysicsSceneSystem::~JoltPhysicsSceneSystem() = default;

JoltPhysicsSceneSystem::JoltPhysicsSceneSystem(JoltPhysicsSceneSystem&&) noexcept = default;

JoltPhysicsSceneSystem& JoltPhysicsSceneSystem::operator=(JoltPhysicsSceneSystem&&) noexcept = default;

void JoltPhysicsSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    kb::scene::PhysicsBackend::RegisterBackend(context.GetScene(), *impl_);
}

void JoltPhysicsSceneSystem::OnFixedUpdate(kb::scene::SceneSystemContext& context) {
    impl_->OnFixedUpdate(context);
}

void JoltPhysicsSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    kb::scene::PhysicsBackend::UnregisterBackend(context.GetScene(), *impl_);
    impl_->OnDestroy();
}

} // namespace kb::physics_jolt
