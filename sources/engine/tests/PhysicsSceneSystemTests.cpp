#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/assets/AssetId.hpp"
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
#include "engine/script/ScriptRuntimeHost.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

    // LIB-126: NonAlloc "all hits" against the REAL Jolt narrow phase -
    // reusing the same box/floor bodies and disjoint layers (0x2/0x1) from
    // the LIB-125 proofs above. A buffer smaller than the real hit count
    // proves genuine truncation against the actual Jolt query, not just a
    // fake backend's own bookkeeping.
    constexpr std::uint32_t kBoxAndFloorLayers = 0x1U | 0x2U;
    std::array<kb::scene::PhysicsCastResult, 4> castAllStorage{};
    kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult> castAllBuffer(castAllStorage);
    kb::scene::PhysicsBackend::CastShapeAll(scene, sphereQueryShape, castOrigin, castDown, 10.0F, kBoxAndFloorLayers, castAllBuffer);
    kb::tests::Require(castAllBuffer.Count() == 2U, "PhysicsBackend::CastShapeAll must collect both the real box and floor bodies when the mask matches both layers");
    kb::tests::Require(castAllBuffer.GetAt(0) != nullptr && castAllBuffer.GetAt(0)->entity == box.Entity(), "PhysicsBackend::CastShapeAll must order the closer real body (the box) first");
    kb::tests::Require(castAllBuffer.GetAt(1) != nullptr && castAllBuffer.GetAt(1)->entity == floor.Entity(), "PhysicsBackend::CastShapeAll must order the farther real body (the floor) second");

    std::array<kb::scene::PhysicsCastResult, 1> smallCastAllStorage{};
    kb::library::ArrayNonAlloc<kb::scene::PhysicsCastResult> smallCastAllBuffer(smallCastAllStorage);
    kb::scene::PhysicsBackend::CastShapeAll(scene, sphereQueryShape, castOrigin, castDown, 10.0F, kBoxAndFloorLayers, smallCastAllBuffer);
    kb::tests::Require(smallCastAllBuffer.Count() == 1U && smallCastAllBuffer.Full(), "PhysicsBackend::CastShapeAll must silently stop at the buffer's capacity against the real Jolt narrow phase too");
    kb::tests::Require(smallCastAllBuffer.GetAt(0) != nullptr && smallCastAllBuffer.GetAt(0)->entity == box.Entity(), "PhysicsBackend::CastShapeAll must keep the closest real body when the buffer can only hold one");

    std::array<kb::scene::PhysicsOverlapResult, 4> overlapAllStorage{};
    kb::library::ArrayNonAlloc<kb::scene::PhysicsOverlapResult> overlapAllBuffer(overlapAllStorage);
    kb::scene::PhysicsBackend::OverlapShapeAll(scene, overlapQueryShape, boxRestingPosition, kBoxAndFloorLayers, overlapAllBuffer);
    kb::tests::Require(overlapAllBuffer.Count() == 2U, "PhysicsBackend::OverlapShapeAll must find both the real box and floor bodies overlapping the query sphere");
    bool overlapAllFoundBox = false;
    bool overlapAllFoundFloor = false;
    for (const kb::scene::PhysicsOverlapResult& overlap : overlapAllBuffer) {
        overlapAllFoundBox = overlapAllFoundBox || overlap.entity == box.Entity();
        overlapAllFoundFloor = overlapAllFoundFloor || overlap.entity == floor.Entity();
    }
    kb::tests::Require(overlapAllFoundBox && overlapAllFoundFloor, "PhysicsBackend::OverlapShapeAll must include both real bodies, regardless of internal order");

    // LIB-127: OnCollisionEnter/Stay/Exit and OnTriggerEnter/Stay/Exit
    // against the REAL Jolt contact listener - reusing this SAME scene (the
    // documented "2 sequential Jolt scenes in one process" bug means every
    // real-Jolt proof in this file must share one scene). A trigger volume
    // and a fresh dynamic "faller" are placed at x=3 (away from the
    // existing floor/box, which sit near the origin) so the interactions
    // are unambiguous: the faller drops through the trigger (Enter then
    // Exit) and then lands on the SAME real floor body (Enter, then Stay
    // while it keeps resting).
    const kb::scene::SceneObject trigger = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Trigger Volume",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 3.0F, 3.0F, 0.0F } },
    });
    scene.Components().Rigidbodies().Set(trigger.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Static });
    scene.Components().Colliders().Set(trigger.Entity(), kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 2.0F, 2.0F, 2.0F },
        .trigger = true,
    });

    const kb::scene::SceneObject faller = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Faller",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 3.0F, 8.0F, 0.0F } },
    });
    scene.Components().Rigidbodies().Set(faller.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F });
    scene.Components().Colliders().Set(faller.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.5F });

    constexpr kb::assets::AssetId kFallerAsset{ 9601U };
    scene.Components().Behaviours().Set(faller.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kFallerAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    kb::script::ScriptRuntimeHost scriptHost{ scene };
    kb::tests::Require(scriptHost.Succeeded(), "LIB-127 real-Jolt test script host did not initialize");

    struct FallerEventRecord {
        std::string name;
        kb::scene::SceneEntity other{};
    };
    std::vector<FallerEventRecord> fallerEvents;
    const auto recordFallerEvent = [&fallerEvents](kb::script::ScriptExecutionContext& context, const kb::script::ScriptEvent& event) {
        static_cast<void>(context);
        kb::scene::SceneEntity other{};
        for (const kb::script::ScriptEventArgument& argument : event.arguments) {
            if (argument.name == "other") {
                other = kb::scene::SceneEntity{ argument.value.AsUInt64() };
            }
        }
        fallerEvents.push_back(FallerEventRecord{ .name = event.name, .other = other });
    };
    for (const char* name : { "OnCollisionEnter", "OnCollisionStay", "OnCollisionExit", "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit" }) {
        kb::tests::Require(scriptHost.NativeBackend().RegisterEvent(kFallerAsset, name, recordFallerEvent), "LIB-127 real-Jolt test event registration failed");
    }
    kb::tests::Require(scriptHost.InstallSceneSystem(), "LIB-127 real-Jolt test scene system install failed");

    for (int i = 0; i < 200; ++i) {
        [[maybe_unused]] const bool fallerProgressed = scene.Runtime().Update(1.0F / 60.0F);
    }

    bool sawTriggerEnter = false;
    bool sawTriggerExit = false;
    bool sawCollisionEnter = false;
    bool sawCollisionStay = false;
    for (const FallerEventRecord& record : fallerEvents) {
        if (record.name == "OnTriggerEnter" && record.other == trigger.Entity()) {
            sawTriggerEnter = true;
        }
        if (record.name == "OnTriggerExit" && record.other == trigger.Entity()) {
            sawTriggerExit = true;
        }
        if (record.name == "OnCollisionEnter" && record.other == floor.Entity()) {
            sawCollisionEnter = true;
        }
        if (record.name == "OnCollisionStay" && record.other == floor.Entity()) {
            sawCollisionStay = true;
        }
    }
    kb::tests::Require(sawTriggerEnter, "Faller must receive a real OnTriggerEnter for the trigger volume it fell through");
    kb::tests::Require(sawTriggerExit, "Faller must receive a real OnTriggerExit after falling all the way through the trigger volume");
    kb::tests::Require(sawCollisionEnter, "Faller must receive a real OnCollisionEnter when it lands on the real floor body");
    kb::tests::Require(sawCollisionStay, "Faller must receive a real OnCollisionStay while it continues resting on the real floor body");

    const auto firstIndexOf = [&fallerEvents](std::string_view name) -> std::size_t {
        for (std::size_t i = 0; i < fallerEvents.size(); ++i) {
            if (fallerEvents[i].name == name) {
                return i;
            }
        }
        return fallerEvents.size();
    };
    kb::tests::Require(firstIndexOf("OnTriggerEnter") < firstIndexOf("OnTriggerExit"), "OnTriggerEnter must be dispatched before OnTriggerExit for the same trigger, in real physics time order");
    kb::tests::Require(firstIndexOf("OnTriggerExit") < firstIndexOf("OnCollisionEnter"), "The faller must exit the trigger before landing on the floor below it, in real physics time order");
}

} // namespace

namespace kb::tests {

void RunPhysicsSceneSystemTests() {
    RunPhysicsSceneSystemFallingBodyTest();
}

} // namespace kb::tests
