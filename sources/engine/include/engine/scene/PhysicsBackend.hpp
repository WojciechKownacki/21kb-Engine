#pragma once

#include "engine/library/EngineLibraryCollections.hpp"
#include "engine/scene/PhysicsLayersAsset.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

    // LIB-129: applies a named-layer interaction matrix to this backend's
    // real collision response (unlike ColliderComponent::layer's existing
    // query-mask use, this changes whether two overlapping bodies actually
    // generate a contact at all). Callable at any point in the backend's
    // lifetime, before or after bodies exist - a later call simply changes
    // behavior for collisions detected from that point on. Default: false
    // (not applied) - the same honest "no-op, not a crash" convention as
    // every other method on this interface when a backend doesn't support
    // something; every real body still gets a named layer from
    // ColliderComponent::layer's lowest set bit regardless of whether this
    // was ever called (default-constructed PhysicsLayersAsset = layer 0,
    // interacts with everything, i.e. today's behavior).
    virtual bool ConfigureLayers(const PhysicsLayersAsset& layers) noexcept {
        static_cast<void>(layers);
        return false;
    }
};

// LIB-127: OnCollisionEnter/Stay/Exit fire for a solid-vs-solid contact;
// OnTriggerEnter/Stay/Exit fire when EITHER collider involved is a trigger
// (ColliderComponent::trigger, already wired to Jolt's Body::IsSensor -
// LIB-123) - the same "either side is a sensor" rule Jolt's own
// ContactListener doc uses. `isTrigger` is the single axis distinguishing
// the two; Enter/Stay/Exit is the same OnContactAdded/Persisted/Removed
// mapping every physics engine with a contact listener uses.
enum class PhysicsContactPhase : std::uint8_t {
    Enter,
    Stay,
    Exit,
};

// Queued by whichever physics plugin is loaded (never constructed by script
// code) via PhysicsBackend::QueueCollisionEvent, and drained once per frame
// by kb::script::ScriptRuntimeSceneSystem, which turns each into a real,
// entity-local ScriptEvent ("OnCollisionEnter" etc., target=`target`) -
// mirrors the SAME producer-queues/consumer-drains-once-per-frame pattern
// SceneState::pendingSceneLifecycleEvents (LIB-073) already established,
// not a new mechanism. `point`/`normal` are only meaningful for
// Enter/Stay (Jolt's OnContactRemoved cannot access body/manifold data at
// all - see JoltPhysicsSceneSystem.cpp) and are zero for Exit.
struct PendingCollisionEvent {
    SceneEntity target;
    SceneEntity other;
    Vec3 point{};
    Vec3 normal{};
    bool isTrigger = false;
    PhysicsContactPhase phase = PhysicsContactPhase::Enter;
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

    // LIB-127: QueueCollisionEvent is called by a physics plugin (e.g.
    // kb_physics_jolt_plugin's contact listener), never by script code.
    // DrainPendingCollisionEvents is called once per frame by
    // kb::script::ScriptRuntimeSceneSystem; returns and clears every event
    // queued since the last drain, in the exact order they were queued.
    static void QueueCollisionEvent(Scene& scene, PendingCollisionEvent event);
    [[nodiscard]] static std::vector<PendingCollisionEvent> DrainPendingCollisionEvents(Scene& scene);

    // LIB-129: stores `layers` on the scene (so LayerBit below can resolve
    // names even without a backend) and applies it to the registered
    // backend's real collision response - the return value reflects ONLY
    // the latter (false if no backend is registered or the backend doesn't
    // support it, see IPhysicsBackend::ConfigureLayers); name resolution via
    // LayerBit is always updated regardless.
    static bool ConfigureLayers(Scene& scene, const PhysicsLayersAsset& layers) noexcept;

    // Resolves a named layer (as last configured via ConfigureLayers /
    // LoadAndConfigureLayers) to its bit value (1 << index), ready to OR
    // into a Physics.*Cast/Overlap layerMask. 0 if the name is unknown.
    // Every scene starts with layer 0 named "Default" (PhysicsLayersAsset's
    // own default), so this always resolves at least that one.
    [[nodiscard]] static std::uint32_t LayerBit(Scene& scene, std::string_view name) noexcept;

    // Convenience: loads a PhysicsLayersAsset by virtual path through the
    // scene's own asset manager (kb::scene::PhysicsLayersAssetLoader, always
    // registered - see Scene's constructor) and applies it. Mirrors
    // kb::project::ProjectDescriptor::physicsLayersAsset - a host calls this
    // explicitly once the project is mounted (scene.Assets().MountProject),
    // the same "activate on demand" shape
    // EditorSceneContext::ActivateProjectInput already uses for
    // inputMappingContext; kb::library does not call this automatically.
    // False if the path is empty, fails to load, or no backend is registered.
    static bool LoadAndConfigureLayers(Scene& scene, const std::string& virtualPath) noexcept;
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
