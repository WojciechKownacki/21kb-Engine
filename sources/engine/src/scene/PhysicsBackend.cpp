#include "engine/scene/PhysicsBackend.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

namespace kb::scene {
namespace {

[[nodiscard]] IPhysicsBackend* FindBackend(Scene& scene) noexcept {
    return SceneAccess::State(scene).physicsBackend;
}

} // namespace

void PhysicsBackend::RegisterBackend(Scene& scene, IPhysicsBackend& backend) {
    SceneAccess::State(scene).physicsBackend = &backend;
}

void PhysicsBackend::UnregisterBackend(Scene& scene, IPhysicsBackend& backend) noexcept {
    SceneState& state = SceneAccess::State(scene);
    if (state.physicsBackend == &backend) {
        state.physicsBackend = nullptr;
    }
}

bool PhysicsBackend::HasBackend(Scene& scene) noexcept {
    return FindBackend(scene) != nullptr;
}

bool PhysicsBackend::AddForce(Scene& scene, SceneEntity entity, Vec3 force) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->AddForce(entity, force);
}

bool PhysicsBackend::AddImpulse(Scene& scene, SceneEntity entity, Vec3 impulse) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->AddImpulse(entity, impulse);
}

bool PhysicsBackend::SetVelocity(Scene& scene, SceneEntity entity, Vec3 velocity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SetVelocity(entity, velocity);
}

PhysicsVectorResult PhysicsBackend::GetVelocity(Scene& scene, SceneEntity entity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr ? backend->GetVelocity(entity) : PhysicsVectorResult{};
}

bool PhysicsBackend::SetAngularVelocity(Scene& scene, SceneEntity entity, Vec3 angularVelocity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->SetAngularVelocity(entity, angularVelocity);
}

PhysicsVectorResult PhysicsBackend::GetAngularVelocity(Scene& scene, SceneEntity entity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr ? backend->GetAngularVelocity(entity) : PhysicsVectorResult{};
}

bool PhysicsBackend::MoveKinematic(Scene& scene, SceneEntity entity, Vec3 targetPosition, Quat targetRotation, float deltaSeconds) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->MoveKinematic(entity, targetPosition, targetRotation, deltaSeconds);
}

bool PhysicsBackend::Sleep(Scene& scene, SceneEntity entity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->Sleep(entity);
}

bool PhysicsBackend::Wake(Scene& scene, SceneEntity entity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->Wake(entity);
}

bool PhysicsBackend::IsSleeping(Scene& scene, SceneEntity entity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->IsSleeping(entity);
}

} // namespace kb::scene
