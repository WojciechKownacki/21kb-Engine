#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class Scene;

struct PhysicsVectorResult {
    bool found = false;
    Vec3 value{};
};

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
};

} // namespace kb::scene
