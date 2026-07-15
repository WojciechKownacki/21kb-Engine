#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <filesystem>
#include <iostream>
#include <utility>

#if !defined(KB_PHYSICS_JOLT_PLUGIN_PATH)
#define KB_PHYSICS_JOLT_PLUGIN_PATH ""
#endif

namespace {

void RunPhysicsSceneSystemFallingBodyTest() {
    if (std::filesystem::path{ KB_PHYSICS_JOLT_PLUGIN_PATH }.empty()) {
        return;
    }

    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Physics.Jolt",
        .binaryPath = KB_PHYSICS_JOLT_PLUGIN_PATH,
        .enabled = true,
    });

    kb::scene::Scene scene{ std::move(descriptor) };

    kb::scene::SceneObject floor = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Floor",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, -0.5F, 0.0F },
        },
    });
    scene.Components().Rigidbodies().Set(floor.Entity(), kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Static,
    });
    scene.Components().Colliders().Set(floor.Entity(), kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 10.0F, 1.0F, 10.0F },
    });

    kb::scene::SceneObject box = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Box",
        .transform = kb::scene::TransformComponent{
            .localPosition = kb::scene::Vec3{ 0.0F, 4.0F, 0.0F },
        },
    });
    scene.Components().Rigidbodies().Set(box.Entity(), kb::scene::RigidbodyComponent{
        .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
        .mass = 1.0F,
    });
    scene.Components().Colliders().Set(box.Entity(), kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
    });

    for (int i = 0; i < 120; ++i) {
        [[maybe_unused]] const bool progressed = scene.Runtime().Update(1.0F / 60.0F);
    }

    const kb::scene::TransformComponent transform = scene.Transforms().Get(box);
    const kb::scene::RigidbodyComponent* rigidbody = scene.Components().Rigidbodies().TryGet(box.Entity());
    if (transform.localPosition.y >= 4.0F || transform.localPosition.y <= 0.35F) {
        std::cerr << "PhysicsSceneSystem final y=" << transform.localPosition.y;
        if (rigidbody != nullptr) {
            std::cerr << " linearVelocityY=" << rigidbody->linearVelocity.y;
        }
        std::cerr << '\n';
    }
    kb::tests::Require(transform.localPosition.y < 4.0F, "PhysicsSceneSystem did not move the dynamic body");
    kb::tests::Require(transform.localPosition.y > 0.35F, "PhysicsSceneSystem let the dynamic body tunnel through the floor");

    // LIB-124: prove kb::scene::PhysicsBackend actually reaches the REAL
    // Jolt backend (JoltPhysicsSceneSystem::Impl), not just that the
    // interface plumbing compiles - reusing the SAME already-settled scene
    // rather than a second Scene (a second sequential Jolt-backed Scene in
    // one process is a known, separately-documented limitation - see
    // _temp.md's "Nierozwiazane ryzyko").
    kb::tests::Require(kb::scene::PhysicsBackend::HasBackend(scene), "PhysicsBackend::HasBackend must report true once the real Jolt plugin is loaded and has run at least one fixed step");

    kb::tests::Require(kb::scene::PhysicsBackend::SetVelocity(scene, box.Entity(), kb::scene::Vec3{ 0.0F, 0.0F, 0.0F }),
        "PhysicsBackend::SetVelocity must report true for the real, live dynamic body");
    const kb::scene::PhysicsVectorResult stoppedVelocity = kb::scene::PhysicsBackend::GetVelocity(scene, box.Entity());
    kb::tests::Require(stoppedVelocity.found && kb::tests::NearlyEqual(stoppedVelocity.value.y, 0.0F),
        "PhysicsBackend::GetVelocity must read back what SetVelocity just wrote through the real Jolt BodyInterface");

    kb::tests::Require(kb::scene::PhysicsBackend::AddImpulse(scene, box.Entity(), kb::scene::Vec3{ 0.0F, 5.0F, 0.0F }),
        "PhysicsBackend::AddImpulse must report true for the real, live dynamic body");
    const kb::scene::PhysicsVectorResult launchedVelocity = kb::scene::PhysicsBackend::GetVelocity(scene, box.Entity());
    kb::tests::Require(launchedVelocity.found && launchedVelocity.value.y > 1.0F,
        "PhysicsBackend::AddImpulse must actually change the real Jolt body's velocity, not just round-trip data - a 5 Ns upward impulse on a mass=1 body must produce a real upward velocity");

    kb::tests::Require(!kb::scene::PhysicsBackend::IsSleeping(scene, box.Entity()), "PhysicsBackend::IsSleeping must report false for a body that was just given velocity");
    kb::tests::Require(kb::scene::PhysicsBackend::Sleep(scene, box.Entity()), "PhysicsBackend::Sleep must report true for the real, live dynamic body");
    kb::tests::Require(kb::scene::PhysicsBackend::IsSleeping(scene, box.Entity()), "PhysicsBackend::IsSleeping must report true immediately after PhysicsBackend::Sleep, through the real Jolt BodyInterface");
    kb::tests::Require(kb::scene::PhysicsBackend::Wake(scene, box.Entity()), "PhysicsBackend::Wake must report true for the real, live dynamic body");
    kb::tests::Require(!kb::scene::PhysicsBackend::IsSleeping(scene, box.Entity()), "PhysicsBackend::IsSleeping must report false immediately after PhysicsBackend::Wake");

    const kb::scene::SceneEntity unknownEntity = scene.Entities().CreateEntity();
    kb::tests::Require(!kb::scene::PhysicsBackend::AddForce(scene, unknownEntity, kb::scene::Vec3{ 1.0F, 0.0F, 0.0F }),
        "PhysicsBackend::AddForce must report false for an entity with no live Jolt body");
}

} // namespace

namespace kb::tests {

void RunPhysicsSceneSystemTests() {
    RunPhysicsSceneSystemFallingBodyTest();
}

} // namespace kb::tests
