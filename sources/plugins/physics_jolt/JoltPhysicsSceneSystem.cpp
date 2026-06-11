#include "JoltPhysicsSceneSystem.hpp"

#include "engine/scene/ColliderComponent.hpp"
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
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
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
        if (instanceCount_ == 0U) {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
        ++instanceCount_;
    }

    ~JoltRuntime() {
        std::lock_guard lock{ mutex_ };
        if (instanceCount_ == 0U) {
            return;
        }
        --instanceCount_;
        if (instanceCount_ == 0U) {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }

    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;

private:
    inline static std::mutex mutex_;
    inline static std::size_t instanceCount_ = 0U;
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
    };
}

[[nodiscard]] bool operator==(const BodySignature& lhs, const BodySignature& rhs) noexcept {
    return lhs.bodyType == rhs.bodyType && lhs.shape == rhs.shape && SameVec3(lhs.scale, rhs.scale) &&
        SameVec3(lhs.center, rhs.center) && SameVec3(lhs.boxSize, rhs.boxSize) && lhs.radius == rhs.radius &&
        lhs.height == rhs.height && lhs.mass == rhs.mass && lhs.gravityScale == rhs.gravityScale &&
        lhs.useGravity == rhs.useGravity && lhs.lockRotation == rhs.lockRotation && lhs.trigger == rhs.trigger;
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

struct TransformCollectContext {
    void* system = nullptr;
    SceneSystemContext* sceneContext = nullptr;
};

} // namespace

class JoltPhysicsSceneSystem::Impl {
public:
    explicit Impl(JoltPhysicsSceneSystemSettings settings)
        : settings_(settings)
        , tempAllocator_(10U * 1024U * 1024U)
        , jobSystem_(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, WorkerThreadCount()) {
        physicsSystem_.Init(MaxBodies, NumBodyMutexes, MaxBodyPairs, MaxContactConstraints, broadPhaseLayers_, objectVsBroadPhaseFilter_, objectLayerPairFilter_);
    }

    ~Impl() {
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

    void SynchronizeBody(SceneEntity entity, TransformComponent& transform, SceneSystemContext& context) {
        const RigidbodyComponent* rigidbody = context.GetScene().Components().Rigidbodies().TryGet(entity);
        const ColliderComponent* collider = context.GetScene().Components().Colliders().TryGet(entity);
        if (rigidbody == nullptr || collider == nullptr) {
            return;
        }

        seenEntities_->insert(entity.Id());
        const BodySignature signature = MakeSignature(*rigidbody, *collider, transform);
        const auto existing = bodies_.find(entity.Id());
        if (existing != bodies_.end() && existing->second.signature == signature) {
            SynchronizeKinematicOrStaticBody(existing->second.bodyId, *rigidbody, *collider, transform, context.DeltaSeconds());
            return;
        }

        if (existing != bodies_.end()) {
            RemoveBody(existing->second.bodyId);
            bodies_.erase(existing);
        }
        bodies_.emplace(entity.Id(), BodyRecord{ .bodyId = CreateBody(*rigidbody, *collider, transform), .signature = signature });
    }

private:
    [[nodiscard]] static std::uint32_t WorkerThreadCount() noexcept {
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        return hardwareThreads <= 1U ? 0U : static_cast<std::uint32_t>(hardwareThreads - 1U);
    }

    void SynchronizeBodies(SceneSystemContext& context) {
        std::unordered_set<std::uint64_t> seen;
        seen.reserve(bodies_.size());
        seenEntities_ = &seen;

        TransformCollectContext collectContext{ .system = this, .sceneContext = &context };
        context.Transforms().ForEachMutable(&SynchronizeTransformBody, &collectContext);
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

    static void SynchronizeTransformBody(SceneEntity entity, TransformComponent& transform, void* rawContext) {
        auto* collectContext = static_cast<TransformCollectContext*>(rawContext);
        if (collectContext == nullptr || collectContext->system == nullptr || collectContext->sceneContext == nullptr) {
            return;
        }
        static_cast<Impl*>(collectContext->system)->SynchronizeBody(entity, transform, *collectContext->sceneContext);
    }

    [[nodiscard]] JPH::BodyID CreateBody(const RigidbodyComponent& rigidbody, const ColliderComponent& collider, const TransformComponent& transform) {
        JPH::RefConst<JPH::Shape> shape = CreateShape(collider, transform.worldScale);
        const Vec3 bodyPosition = Add(transform.worldPosition, collider.center);

        JPH::BodyCreationSettings bodySettings(shape, ToJoltPosition(bodyPosition), ToJolt(transform.worldRotation), ToMotionType(rigidbody.bodyType), ToObjectLayer(rigidbody.bodyType));
        bodySettings.mIsSensor = collider.trigger;
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

            const ColliderComponent* collider = context.GetScene().Components().Colliders().TryGet(entity);
            RigidbodyComponent* rigidbody = context.GetScene().Components().Rigidbodies().TryGet(entity);
            TransformComponent* transform = context.Transforms().TryGet(entity);
            if (collider == nullptr || rigidbody == nullptr || transform == nullptr || rigidbody->bodyType == RigidbodyBodyType::Static) {
                continue;
            }

            const Vec3 position = Subtract(FromJoltPosition(bodyInterface.GetPosition(body.bodyId)), collider->center);
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
    static_cast<void>(context);
}

void JoltPhysicsSceneSystem::OnFixedUpdate(kb::scene::SceneSystemContext& context) {
    impl_->OnFixedUpdate(context);
}

void JoltPhysicsSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    static_cast<void>(context);
    impl_->OnDestroy();
}

} // namespace kb::physics_jolt
