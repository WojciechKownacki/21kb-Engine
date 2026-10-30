#include "TestSupport.hpp"
#include "engine/core/RuntimeState.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneJointComponents.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
kb::project::ProjectDescriptor Descriptor() {
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back({.name = "Physics.Jolt", .binaryPath = KB_PHYSICS_JOLT_PLUGIN_PATH, .enabled = true});
    return descriptor;
}

void RunContinuation(bool pipelined) {
    kb::scene::Scene scene{Descriptor()};
    kb::tests::Require(scene.IsModuleActive("Physics.Jolt"), "Physics state test requires a loaded provider");
    kb::scene::PhysicsBackend::SetStepPipelining(scene, pipelined);
    kb::scene::PhysicsBackend::SetCollisionEventConsumer(scene, true);
    auto* provider = kb::scene::PhysicsBackend::RuntimeStateProvider(scene);
    kb::tests::Require(provider != nullptr, "Loaded physics provider must expose runtime state");
    const auto floor = scene.Entities().CreateObject({.name = "Floor", .transform = {.localPosition = {0,-0.5F,0}}});
    scene.Components().Colliders().Set(floor.Entity(), {.boxSize = {40,1,40}});
    const auto body = scene.Entities().CreateObject({.name = "MovingBody", .transform = {.localPosition = {0,3,0}}});
    scene.Components().Colliders().Set(body.Entity(), {.restitution = 0.35F});
    scene.Components().Rigidbodies().Set(body.Entity(), {.bodyType = kb::scene::RigidbodyBodyType::Dynamic, .linearVelocity = {1,-1,0}});
    const auto pendulum = scene.Entities().CreateObject({.name = "ConstrainedBody", .transform = {.localPosition = {-3,3,0}}});
    scene.Components().Colliders().Set(pendulum.Entity(), {});
    scene.Components().Rigidbodies().Set(pendulum.Entity(), {.bodyType = kb::scene::RigidbodyBodyType::Dynamic, .linearVelocity = {2,0,0}});
    scene.Components().Joints().Set(pendulum.Entity(), {.type = kb::scene::JointType::Distance,
        .connectedAnchor = {-3,5,0}, .minLimit = 2, .maxLimit = 2, .enableLimit = true});
    const auto character = scene.Entities().CreateObject({.name = "Character", .transform = {.localPosition = {5,2,0}}});
    scene.Components().CharacterControllers().Set(character.Entity(), {});
    for (int i = 0; i < 45; ++i) kb::tests::Require(scene.Runtime().Update(1.0F/60.0F), "Physics warmup update failed");
    kb::tests::Require(kb::scene::PhysicsBackend::CharacterMove(scene, character.Entity(), {1,0,0}), "Character move rejected");
    std::string error;
    auto snapshot = provider->CaptureRuntimeState(error);
    kb::tests::Require(snapshot != nullptr && error.empty() && snapshot->PayloadBytes() > 0, "Physics native capture failed");
    const std::array entities{body.Entity(), pendulum.Entity(), character.Entity()};
    auto sample = [&] {
        std::array<float, 12> values{};
        for (std::size_t i = 0; i < entities.size(); ++i) {
            const auto p = scene.Transforms().Get(entities[i]).localPosition;
            values[3*i] = p.x; values[3*i+1] = p.y; values[3*i+2] = p.z;
        }
        const auto velocity = kb::scene::PhysicsBackend::GetVelocity(scene, body.Entity());
        kb::tests::Require(velocity.found, "Physics velocity unavailable");
        values[9] = velocity.value.x; values[10] = velocity.value.y; values[11] = velocity.value.z;
        return values;
    };
    std::vector<std::array<float, 12>> expected;
    for (int i = 0; i < 60; ++i) {
        kb::tests::Require(scene.Runtime().Update(1.0F/60.0F), "Physics forward update failed");
        expected.push_back(sample());
    }
    // A non-physics structural migration invalidates borrowed ECS rows.
    const auto unrelated = scene.Entities().CreateObject({.name = "AdditionalEntity"});
    scene.Entities().Destroy(unrelated);
    const auto started = std::chrono::steady_clock::now();
    kb::tests::Require(provider->RestoreRuntimeState(*snapshot, error) && error.empty(), "Native physics restore failed");
    const auto restoreUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now()-started).count();
    for (std::size_t i = 0; i < expected.size(); ++i) {
        kb::tests::Require(scene.Runtime().Update(1.0F/60.0F), "Physics replay update failed");
        const auto actual = sample();
        for (std::size_t j = 0; j < actual.size(); ++j) {
            if (std::abs(actual[j]-expected[i][j]) > 0.0001F) {
                std::cerr << "Replay frame=" << i << " value=" << j << " expected=" << expected[i][j] << " actual=" << actual[j] << '\n';
                kb::tests::Require(false, "Physics continuation trajectory differs");
            }
        }
    }
    kb::scene::Scene other{Descriptor()};
    auto* otherProvider = kb::scene::PhysicsBackend::RuntimeStateProvider(other);
    kb::tests::Require(otherProvider != nullptr && !otherProvider->RestoreRuntimeState(*snapshot, error) && !error.empty(),
        "Physics snapshot from another provider lifetime was accepted");
    const auto extra = scene.Entities().CreateObject({.name = "AdditionalBody"});
    scene.Components().Colliders().Set(extra.Entity(), {});
    kb::tests::Require(scene.Runtime().Update(1.0F/60.0F), "Topology update failed");
    const auto before = sample();
    kb::tests::Require(!provider->ValidateRuntimeState(*snapshot, error) && !error.empty()
        && !provider->RestoreRuntimeState(*snapshot, error), "Incompatible physics topology was accepted");
    kb::tests::Require(sample() == before, "Rejected physics restore changed native state");

    // Restore authored definitions, then reconstruct the removed native bodies,
    // character and constraint with the identities used by the solver history.
    scene.Entities().Destroy(extra);
    scene.Components().Colliders().Remove(body.Entity());
    scene.Components().CharacterControllers().Remove(character.Entity());
    kb::tests::Require(scene.Runtime().Update(1.0F/60.0F), "Native topology removal update failed");
    scene.Components().Colliders().Set(body.Entity(), {.restitution = 0.35F});
    scene.Components().CharacterControllers().Set(character.Entity(), {});
    const bool rebuilt = provider->RestoreRuntimeState(*snapshot, error);
    if (!rebuilt) std::cerr << "Topology restore: " << error << '\n';
    kb::tests::Require(rebuilt, "Native physics topology reconstruction failed");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        kb::tests::Require(scene.Runtime().Update(1.0F/60.0F), "Reconstructed physics replay update failed");
        const auto actual = sample();
        for (std::size_t j = 0; j < actual.size(); ++j) {
            if (std::abs(actual[j]-expected[i][j]) > 0.0001F) {
                std::cerr << "Rebuilt frame=" << i << " value=" << j << " expected=" << expected[i][j] << " actual=" << actual[j] << '\n';
                kb::tests::Require(false, "Reconstructed physics continuation trajectory differs");
            }
        }
    }
    // Branch history through different shapes. A generation must not be rewound
    // and then reused for a different native topology with the same object count.
    scene.Components().Colliders().Set(body.Entity(), {.shape = kb::scene::ColliderShape::Sphere, .restitution = 0.35F});
    kb::tests::Require(scene.Runtime().Update(1.0F/60.0F), "Sphere branch update failed");
    auto sphereState = provider->CaptureRuntimeState(error);
    kb::tests::Require(sphereState != nullptr, "Sphere branch capture failed");
    const auto queryPoint = scene.Transforms().Get(body).worldPosition + kb::scene::Vec3{0,3,0};
    const auto sphereSurface = kb::scene::PhysicsBackend::ClosestPoint(scene, body.Entity(), queryPoint);
    kb::tests::Require(sphereSurface.found, "Captured sphere query failed");
    scene.Components().Colliders().Set(body.Entity(), {.restitution = 0.35F});
    kb::tests::Require(provider->RestoreRuntimeState(*snapshot, error), "Earlier box branch restore failed");
    scene.Components().Colliders().Set(body.Entity(), {.shape = kb::scene::ColliderShape::Capsule, .radius = 0.4F,
        .height = 1.5F, .restitution = 0.35F});
    kb::tests::Require(scene.Runtime().Update(1.0F/60.0F), "Capsule branch update failed");
    scene.Components().Colliders().Set(body.Entity(), {.shape = kb::scene::ColliderShape::Sphere, .restitution = 0.35F});
    kb::tests::Require(provider->RestoreRuntimeState(*sphereState, error), "Sphere branch restore failed");
    const auto restoredSurface = kb::scene::PhysicsBackend::ClosestPoint(scene, body.Entity(), queryPoint);
    kb::tests::Require(restoredSurface.found && std::abs(restoredSurface.point.x-sphereSurface.point.x) < 0.0001F
        && std::abs(restoredSurface.point.y-sphereSurface.point.y) < 0.0001F
        && std::abs(restoredSurface.point.z-sphereSurface.point.z) < 0.0001F,
        "Branched physics history restored a different native shape");
    std::cout << "Physics state pipelined=" << pipelined << " serialized_bytes=" << snapshot->PayloadBytes()
        << " restore_us=" << restoreUs << " replay_frames=60 rebuilt_replay_frames=60\n";
}

void RunDetachedLifetime() {
    std::unique_ptr<kb::core::RuntimeStateSnapshot> retired;
    std::string error;
    {
        kb::scene::Scene scene{Descriptor()};
        const auto body = scene.Entities().CreateObject();
        scene.Components().Colliders().Set(body.Entity(), {});
        scene.Components().Rigidbodies().Set(body.Entity(), {});
        const auto character = scene.Entities().CreateObject();
        scene.Components().CharacterControllers().Set(character.Entity(), {});
        kb::tests::Require(scene.Runtime().Update(1.0F/60.0F), "Detached snapshot warmup failed");
        retired = kb::scene::PhysicsBackend::RuntimeStateProvider(scene)->CaptureRuntimeState(error);
        kb::tests::Require(retired != nullptr, "Detached snapshot capture failed");
    }
    kb::scene::Scene next{Descriptor()};
    auto* provider = kb::scene::PhysicsBackend::RuntimeStateProvider(next);
    kb::tests::Require(provider != nullptr && !provider->RestoreRuntimeState(*retired, error) && !error.empty(),
        "Snapshot from a destroyed provider was accepted");
    retired.reset();
    std::cout << "Detached physics snapshot lifetime passed\n";
}
}

int main() {
    RunContinuation(false);
    RunContinuation(true);
    RunDetachedLifetime();
}
