#pragma once

#include "engine/library/EngineLibraryCollections.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>

namespace kb::scene {

class Scene;

struct PhysicsVectorResult {
    bool found = false;
    Vec3 value{};
};

// LIB-125: the query SHAPE for CastShape/OverlapShape - a closed, tagged set
// (Sphere/Box/Capsule, matching kb::scene::ColliderShape exactly) rather
// than a virtual Shape type, since ScriptValue crossing the script boundary
// is purely scalar (LIB-058) and every Physics.*Cast/Overlap script function
// needs a flat, fixed field list anyway. Box/Capsule queries are
// axis-aligned (identity orientation) - a deliberately proportionate v1;
// nothing in this task names oriented casts, and adding one is a pure
// additive extension later if a real consumer needs it.
enum class PhysicsShapeKind {
    Sphere,
    Box,
    Capsule,
};

struct PhysicsShapeDesc {
    PhysicsShapeKind kind = PhysicsShapeKind::Sphere;
    float radius = 0.5F; // Sphere, Capsule
    float height = 2.0F; // Capsule - total height including both end caps, same convention as ColliderComponent::height
    Vec3 boxHalfExtents{ 0.5F, 0.5F, 0.5F }; // Box
};

struct PhysicsCastResult {
    bool hit = false;
    SceneEntity entity{};
    float distance = 0.0F;
    Vec3 point{};
    Vec3 normal{};
};

struct PhysicsOverlapResult {
    bool overlapping = false;
    SceneEntity entity{};
};

struct PhysicsClosestPointResult {
    bool found = false;
    Vec3 point{};
    float distance = 0.0F;
};

// A layer mask matching every layer (31 bits set, not 32 - the top bit is
// deliberately left clear so this value stays representable as a positive
// signed int, since layer/layerMask cross the script boundary as
// ScriptValueType::Int via the generic KB_UINT32 property mechanism, which
// rejects negative values on write - see ScriptSceneComponentApi.cpp. 31
// simultaneously-collidable layers is already far beyond what any real
// project configures, so this costs nothing in practice) is the default for
// both a collider's own layer and a query's mask, so existing content and
// existing queries are unaffected until someone deliberately narrows one
// side or the other - LIB-129 owns turning this raw bitmask into named,
// asset-configurable layers.
inline constexpr std::uint32_t kPhysicsAllLayers = 0x7FFFFFFFU;

// LIB-124: kb::engine owns no compile/link-time dependency on any physics
// SDK (confirmed at LIB-123) - force/impulse/velocity/kinematic-move/
// sleep-wake are one-shot or instantaneous-read operations against whatever
// live simulation is actually running, so they need a real, synchronous call
// INTO that simulation right now, not a persistent component field (writing
// RigidbodyComponent.linearVelocity directly is silently inert for a live
// Dynamic body - JoltPhysicsSceneSystem's WriteBack overwrites it from
// Jolt's own state every fixed step). Mirrors kb::audio::IAudioPlaybackBackend
// exactly: a pure-virtual interface kb::engine defines and calls through,
// implemented and registered by whichever physics plugin is actually loaded.
class IPhysicsBackend {
public:
    virtual ~IPhysicsBackend() = default;

    virtual bool AddForce(SceneEntity entity, Vec3 force) noexcept = 0;
    virtual bool AddImpulse(SceneEntity entity, Vec3 impulse) noexcept = 0;
    virtual bool SetVelocity(SceneEntity entity, Vec3 velocity) noexcept = 0;
    [[nodiscard]] virtual PhysicsVectorResult GetVelocity(SceneEntity entity) const noexcept = 0;
    virtual bool SetAngularVelocity(SceneEntity entity, Vec3 angularVelocity) noexcept = 0;
    [[nodiscard]] virtual PhysicsVectorResult GetAngularVelocity(SceneEntity entity) const noexcept = 0;
    // Moves a Kinematic body toward a target pose over deltaSeconds (Jolt's
    // BodyInterface::MoveKinematic - velocity-based, not a teleport). No-op
    // (returns false) for a non-Kinematic body.
    virtual bool MoveKinematic(SceneEntity entity, Vec3 targetPosition, Quat targetRotation, float deltaSeconds) noexcept = 0;
    virtual bool Sleep(SceneEntity entity) noexcept = 0;
    virtual bool Wake(SceneEntity entity) noexcept = 0;
    [[nodiscard]] virtual bool IsSleeping(SceneEntity entity) const noexcept = 0;

    // LIB-125: swept-shape cast (closest hit only, along direction*maxDistance),
    // fixed-position overlap (closest overlapping body), and closest surface
    // point on a specific entity's collider. Real, physics-engine-backed
    // queries (unlike Physics.Raycast, which stays pure ColliderComponent/
    // TransformComponent geometry - a deliberate, unchanged, zero-regression
    // decision; swept-shape collision detection against arbitrary
    // sphere/box/capsule pairs is a fundamentally harder problem this
    // engine's real physics backend already solves correctly).
    [[nodiscard]] virtual PhysicsCastResult CastShape(const PhysicsShapeDesc& shape, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask) const noexcept = 0;
    [[nodiscard]] virtual PhysicsOverlapResult OverlapShape(const PhysicsShapeDesc& shape, Vec3 center, std::uint32_t layerMask) const noexcept = 0;
    [[nodiscard]] virtual PhysicsClosestPointResult ClosestPoint(SceneEntity entity, Vec3 point) const noexcept = 0;

    // LIB-126: "All hits" variants of CastShape/OverlapShape - unlike the
    // closest-hit-only queries above, a call here can genuinely intersect an
    // unbounded number of real bodies, so the result MUST be written into a
    // buffer the caller already owns (kb::library::ArrayNonAlloc<T>, LIB-059
    // - the engine's one existing "hot-path, caller-provided storage,
    // never-allocates" contract) rather than returned as an owning
    // container. This is what "wymaganie bufora" means here: the buffer is
    // a mandatory parameter of the function signature itself, so there is
    // no allocating alternative to reach for by mistake in a Tick. Hits
    // beyond `results.Capacity()` are silently not written (the exact,
    // long-established NonAlloc contract - see Unity's own RaycastNonAlloc);
    // `results.Full()` after the call is the caller's signal that more may
    // exist. Results are ordered closest-first.
    virtual void CastShapeAll(const PhysicsShapeDesc& shape, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask, kb::library::ArrayNonAlloc<PhysicsCastResult>& results) const noexcept = 0;
    virtual void OverlapShapeAll(const PhysicsShapeDesc& shape, Vec3 center, std::uint32_t layerMask, kb::library::ArrayNonAlloc<PhysicsOverlapResult>& results) const noexcept = 0;
};

class PhysicsBackend final {
public:
    PhysicsBackend() = delete;

    static void RegisterBackend(Scene& scene, IPhysicsBackend& backend);
    static void UnregisterBackend(Scene& scene, IPhysicsBackend& backend) noexcept;
    [[nodiscard]] static bool HasBackend(Scene& scene) noexcept;

    static bool AddForce(Scene& scene, SceneEntity entity, Vec3 force) noexcept;
    static bool AddImpulse(Scene& scene, SceneEntity entity, Vec3 impulse) noexcept;
    static bool SetVelocity(Scene& scene, SceneEntity entity, Vec3 velocity) noexcept;
    [[nodiscard]] static PhysicsVectorResult GetVelocity(Scene& scene, SceneEntity entity) noexcept;
    static bool SetAngularVelocity(Scene& scene, SceneEntity entity, Vec3 angularVelocity) noexcept;
    [[nodiscard]] static PhysicsVectorResult GetAngularVelocity(Scene& scene, SceneEntity entity) noexcept;
    static bool MoveKinematic(Scene& scene, SceneEntity entity, Vec3 targetPosition, Quat targetRotation, float deltaSeconds) noexcept;
    static bool Sleep(Scene& scene, SceneEntity entity) noexcept;
    static bool Wake(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool IsSleeping(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static PhysicsCastResult CastShape(Scene& scene, const PhysicsShapeDesc& shape, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask = kPhysicsAllLayers) noexcept;
    [[nodiscard]] static PhysicsOverlapResult OverlapShape(Scene& scene, const PhysicsShapeDesc& shape, Vec3 center, std::uint32_t layerMask = kPhysicsAllLayers) noexcept;
    [[nodiscard]] static PhysicsClosestPointResult ClosestPoint(Scene& scene, SceneEntity entity, Vec3 point) noexcept;

    // LIB-126: honest empty (results.Count()==0) when no backend is
    // registered, same convention as every other facade method above.
    static void CastShapeAll(Scene& scene, const PhysicsShapeDesc& shape, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask, kb::library::ArrayNonAlloc<PhysicsCastResult>& results) noexcept;
    static void OverlapShapeAll(Scene& scene, const PhysicsShapeDesc& shape, Vec3 center, std::uint32_t layerMask, kb::library::ArrayNonAlloc<PhysicsOverlapResult>& results) noexcept;
};

// LIB-126: Raycast has stayed pure ColliderComponent/TransformComponent
// geometry since before IPhysicsBackend existed (LIB-125's own deliberate,
// unchanged decision) - RaycastAllNonAlloc extends that SAME geometry (not
// IPhysicsBackend) to collect every intersecting collider instead of only
// the closest, into a caller-provided buffer, for exactly the reason
// CastShapeAll/OverlapShapeAll above do. Shares its intersection math with
// kb::script::ScriptPhysicsApi's single-hit Physics.Raycast via
// PhysicsGeometryQueries.hpp, not duplicated.
void RaycastAllNonAlloc(Scene& scene, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask, kb::library::ArrayNonAlloc<PhysicsCastResult>& results);

} // namespace kb::scene
