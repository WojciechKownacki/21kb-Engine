#include "engine/scene/PhysicsBackend.hpp"

#include "engine/assets/AssetHandle.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <filesystem>
#include <utility>

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

PhysicsCastResult PhysicsBackend::CastShape(Scene& scene, const PhysicsShapeDesc& shape, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr ? backend->CastShape(shape, origin, direction, maxDistance, layerMask) : PhysicsCastResult{};
}

PhysicsOverlapResult PhysicsBackend::OverlapShape(Scene& scene, const PhysicsShapeDesc& shape, Vec3 center, std::uint32_t layerMask) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr ? backend->OverlapShape(shape, center, layerMask) : PhysicsOverlapResult{};
}

PhysicsClosestPointResult PhysicsBackend::ClosestPoint(Scene& scene, SceneEntity entity, Vec3 point, std::uint32_t layerMask) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr ? backend->ClosestPoint(entity, point, layerMask) : PhysicsClosestPointResult{};
}

// LIB-126: unlike every closest-result method above (which returns a fresh
// struct by value, so "no backend" trivially means a default-constructed
// empty result), these write into a buffer the CALLER already owns and may
// be reusing across many calls - the no-backend branch must explicitly
// clear it, or a Scene that had a backend last Tick but not this one would
// leave stale hits sitting in the caller's buffer.
void PhysicsBackend::CastShapeAll(Scene& scene, const PhysicsShapeDesc& shape, Vec3 origin, Vec3 direction, float maxDistance, std::uint32_t layerMask, kb::library::ArrayNonAlloc<PhysicsCastResult>& results) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    if (backend != nullptr) {
        backend->CastShapeAll(shape, origin, direction, maxDistance, layerMask, results);
    } else {
        results.Clear();
    }
}

void PhysicsBackend::OverlapShapeAll(Scene& scene, const PhysicsShapeDesc& shape, Vec3 center, std::uint32_t layerMask, kb::library::ArrayNonAlloc<PhysicsOverlapResult>& results) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    if (backend != nullptr) {
        backend->OverlapShapeAll(shape, center, layerMask, results);
    } else {
        results.Clear();
    }
}

void PhysicsBackend::QueueCollisionEvent(Scene& scene, PendingCollisionEvent event) {
    SceneAccess::State(scene).pendingCollisionEvents.push_back(std::move(event));
}

std::vector<PendingCollisionEvent> PhysicsBackend::DrainPendingCollisionEvents(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    std::vector<PendingCollisionEvent> drained;
    drained.swap(state.pendingCollisionEvents);
    return drained;
}

bool PhysicsBackend::ConfigureLayers(Scene& scene, const PhysicsLayersAsset& layers) noexcept {
    SceneAccess::State(scene).physicsLayers = layers;
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->ConfigureLayers(layers);
}

std::uint32_t PhysicsBackend::LayerBit(Scene& scene, std::string_view name) noexcept {
    const int index = SceneAccess::State(scene).physicsLayers.LayerIndex(name);
    return index < 0 ? 0U : (1U << static_cast<std::uint32_t>(index));
}

bool PhysicsBackend::LoadAndConfigureLayers(Scene& scene, const std::string& virtualPath) noexcept {
    if (virtualPath.empty()) {
        return false;
    }
    const kb::assets::AssetHandle<PhysicsLayersAsset> handle = scene.Assets().Manager().Load<PhysicsLayersAsset>(std::filesystem::path{ virtualPath });
    if (!handle.IsLoaded()) {
        return false;
    }
    return ConfigureLayers(scene, *handle);
}

bool PhysicsBackend::CharacterMove(Scene& scene, SceneEntity entity, Vec3 horizontalVelocity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->CharacterMove(entity, horizontalVelocity);
}

bool PhysicsBackend::CharacterJump(Scene& scene, SceneEntity entity, float verticalSpeed) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->CharacterJump(entity, verticalSpeed);
}

PhysicsVectorResult PhysicsBackend::CharacterVelocity(Scene& scene, SceneEntity entity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr ? backend->CharacterVelocity(entity) : PhysicsVectorResult{};
}

bool PhysicsBackend::CharacterIsGrounded(Scene& scene, SceneEntity entity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr && backend->CharacterIsGrounded(entity);
}

PhysicsVectorResult PhysicsBackend::CharacterGroundNormal(Scene& scene, SceneEntity entity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr ? backend->CharacterGroundNormal(entity) : PhysicsVectorResult{};
}

PhysicsVectorResult PhysicsBackend::CharacterGroundVelocity(Scene& scene, SceneEntity entity) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr ? backend->CharacterGroundVelocity(entity) : PhysicsVectorResult{};
}

bool PhysicsBackend::QueueCharacterRootMotion(
    Scene& scene, SceneEntity entity, Vec3 localTranslation, Quat localRotation, float durationSeconds) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr &&
        backend->QueueCharacterRootMotion(entity, localTranslation, localRotation, durationSeconds);
}

bool PhysicsBackend::QueueRigidbodyRootMotion(
    Scene& scene, SceneEntity entity, Vec3 localTranslation, Quat localRotation, float durationSeconds) noexcept {
    IPhysicsBackend* backend = FindBackend(scene);
    return backend != nullptr &&
        backend->QueueRigidbodyRootMotion(entity, localTranslation, localRotation, durationSeconds);
}

} // namespace kb::scene
