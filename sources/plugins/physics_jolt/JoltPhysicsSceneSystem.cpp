#include "JoltPhysicsSceneSystem.hpp"

#include "engine/ecs/Query.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
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
#include <Jolt/Physics/Body/AllowedDOFs.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
namespace Layers {
constexpr JPH::ObjectLayer NonMoving = 0;
constexpr JPH::ObjectLayer Moving = 1;
constexpr JPH::ObjectLayer Count = 2;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer NonMoving(0);
constexpr JPH::BroadPhaseLayer Moving(1);
constexpr JPH::uint Count = 2;
} // namespace BroadPhaseLayers

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer object1, JPH::ObjectLayer object2) const override {
        if (object1 == Layers::NonMoving) {
            return object2 == Layers::Moving;
        }
        return object1 == Layers::Moving;
    }
};

class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerInterface() {
        objectToBroadPhase_[Layers::NonMoving] = BroadPhaseLayers::NonMoving;
        objectToBroadPhase_[Layers::Moving] = BroadPhaseLayers::Moving;
    }

    [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::Count;
    }

    [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer < Layers::Count ? objectToBroadPhase_[layer] : BroadPhaseLayers::Moving;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        if (layer == BroadPhaseLayers::NonMoving) {
            return "NonMoving";
        }
        if (layer == BroadPhaseLayers::Moving) {
            return "Moving";
        }
        return "Invalid";
    }
#endif

private:
    JPH::BroadPhaseLayer objectToBroadPhase_[Layers::Count]{};
};

class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer) const override {
        if (objectLayer == Layers::NonMoving) {
            return broadPhaseLayer == BroadPhaseLayers::Moving;
        }
        return objectLayer == Layers::Moving;
    }
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

[[nodiscard]] JPH::ObjectLayer ToObjectLayer(RigidbodyBodyType bodyType) noexcept {
    return bodyType == RigidbodyBodyType::Static ? Layers::NonMoving : Layers::Moving;
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
// Jolt's own ObjectLayer/ObjectLayerFilter is a single coarse value (this
// engine only has 2: Moving/NonMoving, see Layers:: above) and cannot carry
// an arbitrary per-body bitmask, so BodyFilter::ShouldCollideLocked (which
// runs after the body is locked, giving access to GetUserData()) is the
// correct extension point instead.
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

} // namespace

class JoltPhysicsSceneSystem::Impl final : public kb::scene::IPhysicsBackend {
public:
    explicit Impl(JoltPhysicsSceneSystemSettings settings)
        : settings_(settings)
        , tempAllocator_(10U * 1024U * 1024U)
        , jobSystem_(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, WorkerThreadCount()) {
        physicsSystem_.Init(MaxBodies, NumBodyMutexes, MaxBodyPairs, MaxContactConstraints, broadPhaseLayers_, objectVsBroadPhaseFilter_, objectLayerPairFilter_);
        physicsSystem_.SetGravity(JPH::Vec3(0.0F, -9.81F, 0.0F));
    }

    ~Impl() override {
        RemoveAllBodies();
    }

    void OnFixedUpdate(SceneSystemContext& context) {
        SynchronizeBodies(context);
        Step(context.DeltaSeconds());
        WriteBack(context);
    }

    void OnDestroy() {
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

    [[nodiscard]] JPH::BodyID CreateBody(const RigidbodyComponent& rigidbody, const ColliderComponent& collider, const TransformComponent& transform) {
        JPH::RefConst<JPH::Shape> shape = CreateShape(collider, transform.worldScale);
        const Vec3 bodyPosition = Add(transform.worldPosition, collider.center);

        JPH::BodyCreationSettings bodySettings(shape, ToJoltPosition(bodyPosition), ToJolt(transform.worldRotation), ToMotionType(rigidbody.bodyType), ToObjectLayer(rigidbody.bodyType));
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

    void RemoveBody(JPH::BodyID bodyId) {
        if (bodyId.IsInvalid()) {
            return;
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
    BroadPhaseLayerInterface broadPhaseLayers_;
    ObjectVsBroadPhaseLayerFilter objectVsBroadPhaseFilter_;
    ObjectLayerPairFilter objectLayerPairFilter_;
    JPH::PhysicsSystem physicsSystem_;
    JPH::TempAllocatorImpl tempAllocator_;
    JPH::JobSystemThreadPool jobSystem_;
    std::unordered_map<std::uint64_t, BodyRecord> bodies_;
    std::unordered_map<JPH::BodyID, SceneEntity> entityByBodyId_;
    std::vector<PhysicsBodySnapshot> physicsBodyScratch_;
    std::unordered_set<std::uint64_t>* seenEntities_ = nullptr;
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
