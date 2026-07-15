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
        // LIB-125: a distinct, single-bit layer (disjoint from the box's
        // below) so the real-Jolt cast/overlap layer-mask proofs at the end
        // of this test can target one body without the other by layer
        // alone, not merely by "closer hit wins".
        .layer = 0x1U,
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
        .layer = 0x2U,
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

    // LIB-125: prove CastShape/OverlapShape/ClosestPoint reach the REAL
    // Jolt backend, including genuine layer-mask filtering - the floor and
    // box above were given distinct, disjoint single-bit layers at creation
    // (0x1 and 0x2) specifically so a mask matching only one of them proves
    // LayerMaskBodyFilter (JoltPhysicsSceneSystem.cpp) actually discriminates
    // real Jolt bodies by layer, not merely "whichever is hit first".
    const kb::scene::Vec3 boxRestingPosition = scene.Transforms().Get(box).localPosition;
    const kb::scene::Vec3 castOrigin{ boxRestingPosition.x, boxRestingPosition.y + 5.0F, boxRestingPosition.z };
    const kb::scene::Vec3 castDown{ 0.0F, -1.0F, 0.0F };
    const kb::scene::PhysicsShapeDesc sphereQueryShape{ .kind = kb::scene::PhysicsShapeKind::Sphere, .radius = 0.1F };

    const kb::scene::PhysicsCastResult castHitsBox = kb::scene::PhysicsBackend::CastShape(scene, sphereQueryShape, castOrigin, castDown, 10.0F, 0x2U);
    kb::tests::Require(castHitsBox.hit && castHitsBox.entity == box.Entity(), "PhysicsBackend::CastShape with a mask matching only the box's layer must hit the real box body, not the floor");
    kb::tests::Require(castHitsBox.point.y > boxRestingPosition.y - 0.2F, "PhysicsBackend::CastShape hit point must land on the box's real surface, not fall through to the floor");

    const kb::scene::PhysicsCastResult castHitsFloor = kb::scene::PhysicsBackend::CastShape(scene, sphereQueryShape, castOrigin, castDown, 10.0F, 0x1U);
    kb::tests::Require(castHitsFloor.hit && castHitsFloor.entity == floor.Entity(), "PhysicsBackend::CastShape with a mask matching only the floor's layer must hit the real floor body, skipping the box entirely");
    kb::tests::Require(castHitsFloor.point.y > -0.3F && castHitsFloor.point.y < 0.3F, "PhysicsBackend::CastShape must land on the floor's real top surface (y approx 0)");

    const kb::scene::PhysicsCastResult castEmptySpace = kb::scene::PhysicsBackend::CastShape(scene, sphereQueryShape, kb::scene::Vec3{ 1000.0F, 1000.0F, 1000.0F }, castDown, 1.0F, kb::scene::kPhysicsAllLayers);
    kb::tests::Require(!castEmptySpace.hit, "PhysicsBackend::CastShape must honestly report no hit against the real Jolt world when nothing is in range");

    const kb::scene::PhysicsShapeDesc overlapQueryShape{ .kind = kb::scene::PhysicsShapeKind::Sphere, .radius = 0.6F };
    const kb::scene::PhysicsOverlapResult overlapBoxOnly = kb::scene::PhysicsBackend::OverlapShape(scene, overlapQueryShape, boxRestingPosition, 0x2U);
    kb::tests::Require(overlapBoxOnly.overlapping && overlapBoxOnly.entity == box.Entity(), "PhysicsBackend::OverlapShape with a mask matching only the box's layer must overlap the real box body");

    const kb::scene::PhysicsOverlapResult overlapFloorOnly = kb::scene::PhysicsBackend::OverlapShape(scene, overlapQueryShape, boxRestingPosition, 0x1U);
    kb::tests::Require(overlapFloorOnly.overlapping && overlapFloorOnly.entity == floor.Entity(), "PhysicsBackend::OverlapShape with a mask matching only the floor's layer must overlap the real floor body instead of the box");

    const kb::scene::PhysicsClosestPointResult closestOnFloor = kb::scene::PhysicsBackend::ClosestPoint(scene, floor.Entity(), kb::scene::Vec3{ 0.0F, 5.0F, 0.0F });
    kb::tests::Require(closestOnFloor.found, "PhysicsBackend::ClosestPoint must find the real, live floor body");
    kb::tests::Require(closestOnFloor.point.y > -0.3F && closestOnFloor.point.y < 0.3F, "PhysicsBackend::ClosestPoint must return a point on the floor's real top surface (y approx 0)");
    kb::tests::Require(closestOnFloor.distance > 4.5F && closestOnFloor.distance < 5.5F, "PhysicsBackend::ClosestPoint must return the real distance from the query point to the floor's surface");

    const kb::scene::PhysicsClosestPointResult closestOnUnknown = kb::scene::PhysicsBackend::ClosestPoint(scene, unknownEntity, kb::scene::Vec3{ 0.0F, 5.0F, 0.0F });
    kb::tests::Require(!closestOnUnknown.found, "PhysicsBackend::ClosestPoint must report found=false for an entity with no live Jolt body");
}

} // namespace

namespace kb::tests {

void RunPhysicsSceneSystemTests() {
    RunPhysicsSceneSystemFallingBodyTest();
}

} // namespace kb::tests
