#include "SceneSystemTestSuites.hpp"
#include "TestSupport.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/PhysicsLayersAsset.hpp"
#include "engine/scene/PhysicsLayersAssetIO.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

    // LIB-014: the Projectile template's REAL physics-driven proof - reusing
    // this SAME scene and the SAME scriptHost already attached above (a
    // second ScriptRuntimeHost on one Scene is untested territory, and
    // reusing the existing one is also simply correct: it is still the
    // same live scene). Placed at y=15, x/z far from the floor's +-5
    // footprint and the trigger/faller rig near x=3, so its straight-line
    // flight cannot touch anything but its own target. useGravity=false
    // keeps the flight path exactly straight, so a real-Jolt hit is a
    // deterministic distance/time away rather than a ballistic arc this
    // test would need to compute.
    const kb::scene::SceneObject projectile = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Projectile",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ -8.0F, 15.0F, -8.0F } },
    });
    scene.Components().Rigidbodies().Set(projectile.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 0.5F, .useGravity = false });
    scene.Components().Colliders().Set(projectile.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.3F });

    constexpr kb::assets::AssetId kProjectileAsset{ 9602U };
    scene.Components().Behaviours().Set(projectile.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kProjectileAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    const kb::scene::SceneObject target = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Target",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ -2.0F, 15.0F, -8.0F } },
    });
    scene.Components().Rigidbodies().Set(target.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Static });
    scene.Components().Colliders().Set(target.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Box, .boxSize = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F } });

    // LIB-014: the launch retries in Tick (not a one-shot in Ready) because
    // a freshly-spawned entity's Rigidbody/Collider is not guaranteed to
    // have a live Jolt body yet by the time Ready fires - the physics and
    // script scene systems' relative execution order is not guaranteed
    // (see LIB-127's own notes; LIB-128's job to formalize). Retrying every
    // Tick until Physics.SetVelocity actually reports applied=true is both
    // the robust fix and realistic template code a real project would want
    // anyway, not merely a test workaround.
    const std::string projectileLuaScript =
        "local launched = false\n"
        "function Tick(self, dt)\n"
        "    if not launched then\n"
        "        local applied = Physics.SetVelocity(self.entity, 5.0, 0.0, 0.0)\n"
        "        if applied then\n"
        "            launched = true\n"
        "        end\n"
        "    end\n"
        "end\n"
        "function OnCollisionEnter(self, event)\n"
        "    SetShared(\"projectileHit\", true)\n"
        "    SetShared(\"projectileHitOther\", event.args.other)\n"
        "    World.Destroy(self.entity)\n"
        "end\n";
    kb::tests::Require(scriptHost.LuaRuntime().LoadScript(kProjectileAsset, projectileLuaScript).succeeded, "LIB-014 projectile template Lua script did not load");

    for (int i = 0; i < 150; ++i) {
        [[maybe_unused]] const bool projectileProgressed = scene.Runtime().Update(1.0F / 60.0F);
    }

    const std::optional<kb::script::ScriptValue> projectileHit = scriptHost.SharedState().Get("projectileHit");
    kb::tests::Require(projectileHit.has_value() && projectileHit->AsBool(), "LIB-014 projectile template must receive a real OnCollisionEnter when it hits the target");
    const std::optional<kb::script::ScriptValue> projectileHitOther = scriptHost.SharedState().Get("projectileHitOther");
    kb::tests::Require(projectileHitOther.has_value() && static_cast<std::uint64_t>(projectileHitOther->AsInt()) == target.Entity().Id(),
        "LIB-014 projectile template's OnCollisionEnter must report the real target entity it hit");
    kb::tests::Require(!scene.Entities().IsAlive(projectile.Entity()), "LIB-014 projectile template must destroy itself via World.Destroy after a real collision");

    // LIB-015: sample scene end-to-end - input -> movement -> spawn -> real
    // collision -> log, chaining LIB-014's Projectile template behind a
    // real Input action instead of spawning it directly from C++. Reuses
    // this SAME scene/scriptHost/Target (the "2 sequential Jolt scenes"
    // constraint - see notes above) - the LIB-014 projectile that used to
    // occupy (-8,15,-8) is already destroyed, so a freshly spawned one can
    // safely reuse that same start point and fly at the same still-alive
    // Target.
    const std::filesystem::path sampleProjectRoot = std::filesystem::temp_directory_path() / "21kb_engine_physics_scene_tests_lib015";
    std::error_code sampleResetError;
    std::filesystem::remove_all(sampleProjectRoot, sampleResetError);
    std::filesystem::create_directories(sampleProjectRoot / "Assets" / "Logic", sampleResetError);
    kb::tests::Require(!sampleResetError, "LIB-015 sample scene project root could not be prepared");

    {
        std::ofstream spawnScriptFile{ sampleProjectRoot / "Assets" / "Logic" / "ProjectileSpawn.lua", std::ios::binary | std::ios::trunc };
        kb::tests::Require(spawnScriptFile.is_open(), "LIB-015 projectile spawn script could not be opened for writing");
        spawnScriptFile << "local launched = false\n"
                           "function Tick(self, dt)\n"
                           "    if not launched then\n"
                           "        local applied = Physics.SetVelocity(self.entity, 5.0, 0.0, 0.0)\n"
                           "        if applied then\n"
                           "            launched = true\n"
                           "        end\n"
                           "    end\n"
                           "end\n"
                           "function OnCollisionEnter(self, event)\n"
                           "    SetShared(\"sampleProjectileHit\", true)\n"
                           "    SetShared(\"sampleProjectileHitOther\", event.args.other)\n"
                           "    World.Destroy(self.entity)\n"
                           "end\n";
        kb::tests::Require(spawnScriptFile.good(), "LIB-015 projectile spawn script could not be written");
    }

    // AssetId is a deterministic hash of the virtual path (LIB-009) - a
    // throwaway discovery scene resolves the SAME id the shared `scene`
    // will later discover when it mounts this same project, so the
    // prefab's baked BehaviourComponent can reference it up front.
    kb::assets::AssetId sampleSpawnScriptAssetId{};
    {
        kb::scene::Scene discoveryScene;
        kb::tests::Require(discoveryScene.Assets().MountProject(sampleProjectRoot), "LIB-015 sample scene project mount (discovery) failed");
        kb::tests::Require(discoveryScene.Assets().Discover() == 1U, "LIB-015 sample scene did not discover exactly the projectile spawn script");
        const kb::assets::AssetMetadata* metadata = discoveryScene.Assets().Manager().Registry().FindByPath("/Game/Logic/ProjectileSpawn.lua");
        kb::tests::Require(metadata != nullptr, "LIB-015 sample scene could not resolve the projectile spawn script's asset id");
        sampleSpawnScriptAssetId = metadata->id;
    }

    {
        kb::scene::Scene prefabSource;
        const kb::scene::SceneObject prefabRoot = prefabSource.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ProjectileSpawn" });
        prefabSource.Components().Rigidbodies().Set(prefabRoot.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 0.5F, .useGravity = false });
        prefabSource.Components().Colliders().Set(prefabRoot.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.3F });
        prefabSource.Components().Behaviours().Set(prefabRoot.Entity(), kb::scene::BehaviourComponent{
            .behaviourAssetId = sampleSpawnScriptAssetId.value,
            .backend = kb::scene::BehaviourBackend::Lua,
            .enabled = true,
        });
        const kb::scene::ScenePrefabHandle prefab = prefabSource.Prefabs().CaptureRegistered(prefabRoot, "ProjectileSpawn");
        std::error_code prefabDirError;
        std::filesystem::create_directories(sampleProjectRoot / "Assets" / "Prefabs", prefabDirError);
        kb::tests::Require(!prefabDirError, "LIB-015 sample scene prefab directory could not be created");
        kb::tests::Require(prefabSource.Prefabs().Save(prefab, sampleProjectRoot / "Assets" / "Prefabs" / "ProjectileSpawn.kbprefab"), "LIB-015 projectile spawn prefab could not be saved");
    }

    kb::tests::Require(scene.Assets().MountProject(sampleProjectRoot), "LIB-015 sample scene project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 2U, "LIB-015 sample scene did not discover both the script and the prefab");

    // --- Input: a real "Fire" action bound to a real key, evaluated
    // through the real InputSubsystem - not a fabricated shortcut.
    using kb::input::InputActionAsset;
    using kb::input::InputActionValueType;
    using kb::input::InputKey;
    using kb::input::InputKeyMapping;
    using kb::input::InputMappingContextAsset;

    auto fireAction = std::make_shared<InputActionAsset>();
    fireAction->name = "Fire";
    fireAction->valueType = InputActionValueType::Bool;

    auto fireContext = std::make_shared<InputMappingContextAsset>();
    fireContext->mappings.push_back(InputKeyMapping{ .actionId = 1U, .key = InputKey::F });

    std::unordered_map<std::uint64_t, std::shared_ptr<InputActionAsset>> sampleActions{ { 1U, fireAction } };
    std::unordered_map<std::uint64_t, std::shared_ptr<InputMappingContextAsset>> sampleContexts{ { 60U, fireContext } };
    scene.Input().SetResolvers(
        [&sampleActions](std::uint64_t id) -> std::shared_ptr<const InputActionAsset> {
            const auto found = sampleActions.find(id);
            return found != sampleActions.end() ? found->second : nullptr;
        },
        [&sampleContexts](std::uint64_t id) -> std::shared_ptr<const InputMappingContextAsset> {
            const auto found = sampleContexts.find(id);
            return found != sampleContexts.end() ? found->second : nullptr;
        });
    kb::tests::Require(scene.Input().AddMappingContext(60U, 0), "LIB-015 sample scene could not add its Fire mapping context");
    scene.Input().MutableDeviceState().SetKeyDown(InputKey::F, true);
    scene.Input().Evaluate(1.0F / 60.0F);

    // --- Player: reads the real Fire action and spawns the projectile
    // prefab exactly once, at the SAME start point/target as LIB-014's
    // direct-spawn proof above - the "spawn" step this sample adds on top.
    constexpr kb::assets::AssetId kPlayerAsset{ 9603U };
    const kb::scene::SceneObject player = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Player" });
    scene.Components().Behaviours().Set(player.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kPlayerAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    const std::string playerLuaScript =
        "local fired = false\n"
        "function Tick(self, dt)\n"
        "    if not fired and Input.ActionBool(\"Fire\") then\n"
        "        fired = true\n"
        "        local spawned = World.InstantiatePrefab({ prefab = \"/Game/Prefabs/ProjectileSpawn.kbprefab\", x = -8.0, y = 15.0, z = -8.0 })\n"
        "        SetShared(\"sampleSpawnedEntity\", spawned)\n"
        "    end\n"
        "end\n";
    kb::tests::Require(scriptHost.LuaRuntime().LoadScript(kPlayerAsset, playerLuaScript).succeeded, "LIB-015 sample scene player script did not load");

    for (int i = 0; i < 150; ++i) {
        [[maybe_unused]] const bool sampleProgressed = scene.Runtime().Update(1.0F / 60.0F);
    }

    const std::optional<kb::script::ScriptValue> spawnedEntityValue = scriptHost.SharedState().Get("sampleSpawnedEntity");
    kb::tests::Require(spawnedEntityValue.has_value(), "LIB-015 sample scene player must spawn the projectile once Fire is read as pressed");
    // LIB-015: SetShared("sampleSpawnedEntity", spawned) re-marshals the
    // returned entity id through Lua's generic value bridge, which infers
    // ScriptValueType purely from the Lua number's own magnitude (the SAME
    // pre-existing gap LIB-123/124/125 documented for entity ARGUMENTS,
    // hit here for an entity RETURN VALUE instead) - a small entity id
    // comes back tagged Int, not Entity, so it must be read via AsInt(),
    // not AsUInt64() (which only reads the Entity/Component/Hash/UInt32
    // variant slot and would silently return the 0 fallback here).
    const kb::scene::SceneEntity spawnedProjectile{ static_cast<std::uint64_t>(spawnedEntityValue->AsInt()) };
    kb::tests::Require(spawnedProjectile.IsValid(), "LIB-015 sample scene World.InstantiatePrefab must return a real spawned entity");

    const std::optional<kb::script::ScriptValue> sampleHit = scriptHost.SharedState().Get("sampleProjectileHit");
    kb::tests::Require(sampleHit.has_value() && sampleHit->AsBool(), "LIB-015 sample scene's spawned projectile must receive a real OnCollisionEnter when it hits the target");
    const std::optional<kb::script::ScriptValue> sampleHitOther = scriptHost.SharedState().Get("sampleProjectileHitOther");
    kb::tests::Require(sampleHitOther.has_value() && static_cast<std::uint64_t>(sampleHitOther->AsInt()) == target.Entity().Id(),
        "LIB-015 sample scene's spawned projectile must report the real target entity it hit");
    kb::tests::Require(!scene.Entities().IsAlive(spawnedProjectile), "LIB-015 sample scene's spawned projectile must destroy itself via World.Destroy after a real collision");

    // LIB-129: named collision layers + interaction matrix, against the SAME
    // real Jolt backend (see the "2 sequential Jolt scenes" note above - one
    // more reason every real-Jolt proof in this file shares this one
    // scene). Two pairs of overlapping, gravity-free dynamic spheres far
    // from every other body in this test (x=20/22): "PhaseA"/"PhaseB" sit on
    // two named layers whose interaction is explicitly disabled;
    // "SolidA"/"SolidB" sit on the same default layer as a positive control.
    // If ConfigureLayers had no real effect on Jolt's collision response,
    // both pairs would separate identically; if it works, only the
    // interacting (default-layer) pair separates.
    kb::scene::PhysicsLayersAsset layers;
    constexpr std::uint32_t kPhaseLayerA = 5U;
    constexpr std::uint32_t kPhaseLayerB = 6U;
    layers.layerNames[kPhaseLayerA] = "PhaseA";
    layers.layerNames[kPhaseLayerB] = "PhaseB";
    layers.SetLayersInteract(kPhaseLayerA, kPhaseLayerB, false);
    kb::tests::Require(kb::scene::PhysicsBackend::ConfigureLayers(scene, layers), "LIB-129 PhysicsBackend::ConfigureLayers must report true against the real Jolt backend");
    kb::tests::Require(kb::scene::PhysicsBackend::LayerBit(scene, "PhaseA") == (1U << kPhaseLayerA), "LIB-129 PhysicsBackend::LayerBit must resolve a configured layer name to its real bit value");
    kb::tests::Require(kb::scene::PhysicsBackend::LayerBit(scene, "PhaseB") == (1U << kPhaseLayerB), "LIB-129 PhysicsBackend::LayerBit must resolve the second configured layer name too");
    kb::tests::Require(kb::scene::PhysicsBackend::LayerBit(scene, "NoSuchLayer") == 0U, "LIB-129 PhysicsBackend::LayerBit must return 0 for an unknown layer name");
    kb::tests::Require(kb::scene::PhysicsBackend::LayerBit(scene, "Default") == 1U, "LIB-129 PhysicsBackend::LayerBit must resolve the default layer 0 name every scene starts with");

    const kb::scene::SceneObject phaseA = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "PhaseA",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 20.0F, 5.0F, 0.0F } },
    });
    scene.Components().Rigidbodies().Set(phaseA.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F, .useGravity = false });
    scene.Components().Colliders().Set(phaseA.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.5F, .layer = 1U << kPhaseLayerA });

    const kb::scene::SceneObject phaseB = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "PhaseB",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 20.0F, 5.3F, 0.0F } },
    });
    scene.Components().Rigidbodies().Set(phaseB.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F, .useGravity = false });
    scene.Components().Colliders().Set(phaseB.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.5F, .layer = 1U << kPhaseLayerB });

    const kb::scene::SceneObject solidA = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "SolidA",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 22.0F, 5.0F, 0.0F } },
    });
    scene.Components().Rigidbodies().Set(solidA.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F, .useGravity = false });
    scene.Components().Colliders().Set(solidA.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.5F });

    const kb::scene::SceneObject solidB = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "SolidB",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 22.0F, 5.3F, 0.0F } },
    });
    scene.Components().Rigidbodies().Set(solidB.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F, .useGravity = false });
    scene.Components().Colliders().Set(solidB.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.5F });

    for (int i = 0; i < 60; ++i) {
        [[maybe_unused]] const bool progressed = scene.Runtime().Update(1.0F / 60.0F);
    }

    const float phaseSeparation = std::fabs(scene.Transforms().Get(phaseA).localPosition.y - scene.Transforms().Get(phaseB).localPosition.y);
    const float solidSeparation = std::fabs(scene.Transforms().Get(solidA).localPosition.y - scene.Transforms().Get(solidB).localPosition.y);
    kb::tests::Require(phaseSeparation < 0.32F, "LIB-129 two overlapping bodies on layers configured NOT to interact must NOT receive any real Jolt separation response");
    kb::tests::Require(solidSeparation > 0.9F, "LIB-129 two overlapping bodies on the same (default, interacting) layer must receive a real Jolt separation response - positive control proving the harness itself can detect a collision response");

    const std::vector<kb::scene::PendingCollisionEvent> phaseEvents = kb::scene::PhysicsBackend::DrainPendingCollisionEvents(scene);
    for (const kb::scene::PendingCollisionEvent& event : phaseEvents) {
        const bool involvesNonInteractingPair = (event.target == phaseA.Entity() && event.other == phaseB.Entity()) ||
            (event.target == phaseB.Entity() && event.other == phaseA.Entity());
        kb::tests::Require(!involvesNonInteractingPair, "LIB-129 two overlapping bodies on non-interacting layers must never raise a real OnCollisionEnter/Stay/Exit against each other");
    }

    // LIB-130: constraints/joints - the four "faktycznie obslugiwane typy"
    // (Fixed/Hinge/Distance/Point), each proven against the REAL Jolt
    // constraint solver via kb::scene::JointComponent (LIB-123's existing
    // data-only component, now actually simulated). New bodies sit at
    // x=30..46, clear of every other body already in this scene.

    // Point-to-world: connectedEntity left invalid (JointComponent's own
    // documented "connect to the static world" case), anchor=body's own
    // origin, connectedAnchor=the body's own starting world position - a
    // zero-length pivot arm, so a real Point constraint should hold it
    // exactly in place against gravity instead of letting it fall.
    const kb::scene::Vec3 pointStart{ 30.0F, 10.0F, 0.0F };
    const kb::scene::SceneObject pointBody = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "PointJointed",
        .transform = kb::scene::TransformComponent{ .localPosition = pointStart },
    });
    scene.Components().Rigidbodies().Set(pointBody.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F, .useGravity = true });
    scene.Components().Colliders().Set(pointBody.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.3F });
    scene.Components().Joints().Set(pointBody.Entity(), kb::scene::JointComponent{
        .type = kb::scene::JointType::Point,
        .connectedEntity = {},
        .anchor = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F },
        .connectedAnchor = pointStart,
    });

    // Fixed between two dynamic bodies: A and B start 1 unit apart in Y with
    // no support - BOTH should fall together under gravity (proving the
    // constraint does not itself hold anything up, unlike the world-jointed
    // case above), while their separation stays rigidly ~1.0 throughout
    // (proving the weld couples their motion instead of letting them fall
    // independently).
    const kb::scene::SceneObject weldedA = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "WeldedA",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 34.0F, 10.0F, 0.0F } },
    });
    scene.Components().Rigidbodies().Set(weldedA.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F, .useGravity = true });
    scene.Components().Colliders().Set(weldedA.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.3F });

    const kb::scene::SceneObject weldedB = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "WeldedB",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 34.0F, 9.0F, 0.0F } },
    });
    scene.Components().Rigidbodies().Set(weldedB.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F, .useGravity = true });
    scene.Components().Colliders().Set(weldedB.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.3F });
    scene.Components().Joints().Set(weldedB.Entity(), kb::scene::JointComponent{
        .type = kb::scene::JointType::Fixed,
        .connectedEntity = weldedA.Entity(),
        .anchor = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F },
        .connectedAnchor = kb::scene::Vec3{ 0.0F, -1.0F, 0.0F },
    });

    // Distance-to-world with an explicit limit: free-falls until 3 units
    // from its own starting point, then must stay bounded there (a rope/
    // chain, not an infinite fall) - the clearest possible proof that
    // minLimit/maxLimit are actually consumed, not merely stored data.
    const kb::scene::Vec3 distanceAnchor{ 38.0F, 10.0F, 0.0F };
    const kb::scene::SceneObject distanceBody = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "DistanceJointed",
        .transform = kb::scene::TransformComponent{ .localPosition = distanceAnchor },
    });
    scene.Components().Rigidbodies().Set(distanceBody.Entity(), kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F, .useGravity = true });
    scene.Components().Colliders().Set(distanceBody.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.3F });
    scene.Components().Joints().Set(distanceBody.Entity(), kb::scene::JointComponent{
        .type = kb::scene::JointType::Distance,
        .connectedEntity = {},
        .anchor = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F },
        .connectedAnchor = distanceAnchor,
        .minLimit = 0.0F,
        .maxLimit = 3.0F,
        .enableLimit = true,
    });

    // Hinge limit vs. no limit: both spin around a world-jointed pivot at
    // their own origin (zero arm, so position stays put - isolates the test
    // to rotation) starting with the SAME angular velocity around the hinge
    // axis; the narrowly-limited one's real Jolt constraint must arrest
    // that spin once it hits its limit, while the unlimited one keeps
    // spinning freely - a positive/negative control pair (mirrors LIB-129's
    // phase/solid comparison) rather than asserting one absolute value.
    const kb::scene::Vec3 hingeAxis{ 0.0F, 1.0F, 0.0F };
    const kb::scene::RigidbodyComponent hingeSpin{ .bodyType = kb::scene::RigidbodyBodyType::Dynamic, .mass = 1.0F, .angularVelocity = kb::scene::Vec3{ 0.0F, 10.0F, 0.0F }, .useGravity = false };

    const kb::scene::Vec3 hingeLimitedStart{ 42.0F, 10.0F, 0.0F };
    const kb::scene::SceneObject hingeLimited = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "HingeLimited",
        .transform = kb::scene::TransformComponent{ .localPosition = hingeLimitedStart },
    });
    scene.Components().Rigidbodies().Set(hingeLimited.Entity(), hingeSpin);
    scene.Components().Colliders().Set(hingeLimited.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.3F });
    scene.Components().Joints().Set(hingeLimited.Entity(), kb::scene::JointComponent{
        .type = kb::scene::JointType::Hinge,
        .connectedEntity = {},
        .anchor = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F },
        .connectedAnchor = hingeLimitedStart,
        .axis = hingeAxis,
        .minLimit = -5.0F,
        .maxLimit = 5.0F,
        .enableLimit = true,
    });

    const kb::scene::Vec3 hingeFreeStart{ 46.0F, 10.0F, 0.0F };
    const kb::scene::SceneObject hingeFree = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "HingeFree",
        .transform = kb::scene::TransformComponent{ .localPosition = hingeFreeStart },
    });
    scene.Components().Rigidbodies().Set(hingeFree.Entity(), hingeSpin);
    scene.Components().Colliders().Set(hingeFree.Entity(), kb::scene::ColliderComponent{ .shape = kb::scene::ColliderShape::Sphere, .radius = 0.3F });
    scene.Components().Joints().Set(hingeFree.Entity(), kb::scene::JointComponent{
        .type = kb::scene::JointType::Hinge,
        .connectedEntity = {},
        .anchor = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F },
        .connectedAnchor = hingeFreeStart,
        .axis = hingeAxis,
        .enableLimit = false,
    });

    for (int i = 0; i < 90; ++i) {
        [[maybe_unused]] const bool progressed = scene.Runtime().Update(1.0F / 60.0F);
    }

    const kb::scene::Vec3 pointFinal = scene.Transforms().Get(pointBody).localPosition;
    kb::tests::Require(std::fabs(pointFinal.y - pointStart.y) < 0.1F, "LIB-130 a real Jolt Point constraint to the world must hold its body in place against gravity");

    const kb::scene::Vec3 weldedAFinal = scene.Transforms().Get(weldedA).localPosition;
    const kb::scene::Vec3 weldedBFinal = scene.Transforms().Get(weldedB).localPosition;
    kb::tests::Require(weldedAFinal.y < 8.0F, "LIB-130 a Fixed-jointed pair connected to nothing else must still fall under gravity, not be held up by the joint itself");
    const float weldedSeparation = std::fabs(weldedAFinal.y - weldedBFinal.y);
    kb::tests::Require(weldedSeparation > 0.85F && weldedSeparation < 1.15F, "LIB-130 a real Jolt Fixed constraint must rigidly preserve the relative offset between two dynamic bodies as they fall together");

    const kb::scene::Vec3 distanceFinal = scene.Transforms().Get(distanceBody).localPosition;
    const float distanceFromAnchor = std::sqrt(
        (distanceFinal.x - distanceAnchor.x) * (distanceFinal.x - distanceAnchor.x) +
        (distanceFinal.y - distanceAnchor.y) * (distanceFinal.y - distanceAnchor.y) +
        (distanceFinal.z - distanceAnchor.z) * (distanceFinal.z - distanceAnchor.z));
    kb::tests::Require(distanceFromAnchor < 3.3F, "LIB-130 a real Jolt Distance constraint's maxDistance must actually bound how far the body falls");
    kb::tests::Require(distanceFromAnchor > 2.5F, "LIB-130 a Distance-jointed body must actually fall toward its limit under gravity, not be held motionless like a Point joint");

    const kb::scene::RigidbodyComponent* hingeLimitedBody = scene.Components().Rigidbodies().TryGet(hingeLimited.Entity());
    const kb::scene::RigidbodyComponent* hingeFreeBody = scene.Components().Rigidbodies().TryGet(hingeFree.Entity());
    kb::tests::Require(hingeLimitedBody != nullptr && hingeFreeBody != nullptr, "LIB-130 hinge test bodies must still have real Jolt bodies after simulation");
    const float hingeLimitedSpin = std::fabs(hingeLimitedBody->angularVelocity.y);
    const float hingeFreeSpin = std::fabs(hingeFreeBody->angularVelocity.y);
    kb::tests::Require(hingeFreeSpin > 5.0F, "LIB-130 an unlimited real Jolt Hinge constraint must let the body keep spinning near its initial angular velocity");
    kb::tests::Require(hingeLimitedSpin < hingeFreeSpin * 0.5F, "LIB-130 a real Jolt Hinge constraint's limit must actually arrest the spin once it is reached, unlike the unlimited positive control");

    // Removal: tearing down the JointComponent must let gravity resume on
    // the previously-held Point-jointed body.
    scene.Components().Joints().Remove(pointBody.Entity());
    for (int i = 0; i < 30; ++i) {
        [[maybe_unused]] const bool progressed = scene.Runtime().Update(1.0F / 60.0F);
    }
    kb::tests::Require(scene.Transforms().Get(pointBody).localPosition.y < pointFinal.y - 0.2F, "LIB-130 removing a JointComponent must actually tear down the real Jolt constraint, letting gravity resume");
}

// LIB-129: pure asset IO/loader coverage - unlike the real-Jolt test above,
// this needs no physics plugin at all, so it always runs.
void RunPhysicsLayersAssetIOTest() {
    kb::scene::PhysicsLayersAsset asset;
    asset.layerNames[3] = "Enemy";
    asset.layerNames[4] = "Player";
    asset.SetLayersInteract(3, 4, false);

    const std::vector<std::uint8_t> encoded = kb::scene::EncodePhysicsLayersAsset(asset);
    const kb::scene::PhysicsLayersAssetLoadResult decoded = kb::scene::DecodePhysicsLayersAsset(encoded);
    kb::tests::Require(decoded.succeeded, "LIB-129 physics layers asset must decode what it just encoded");
    kb::tests::Require(decoded.asset.layerNames[3] == "Enemy", "LIB-129 physics layers asset layer names must round-trip");
    kb::tests::Require(decoded.asset.layerNames[4] == "Player", "LIB-129 physics layers asset layer names must round-trip (second layer)");
    kb::tests::Require(decoded.asset.layerNames[0] == "Default", "LIB-129 physics layers asset default layer 0 name must round-trip");
    kb::tests::Require(!decoded.asset.LayersInteract(3, 4), "LIB-129 physics layers asset disabled interaction must round-trip");
    kb::tests::Require(!decoded.asset.LayersInteract(4, 3), "LIB-129 physics layers asset disabled interaction must round-trip symmetrically");
    kb::tests::Require(decoded.asset.LayersInteract(0, 1), "LIB-129 physics layers asset untouched pairs must still default to interacting");
    kb::tests::Require(decoded.asset.LayerIndex("Enemy") == 3, "LIB-129 physics layers asset LayerIndex must resolve a round-tripped name");
    kb::tests::Require(decoded.asset.LayerIndex("Missing") == -1, "LIB-129 physics layers asset LayerIndex must return -1 for an unknown name");

    const kb::scene::PhysicsLayersAssetLoadResult corrupt = kb::scene::DecodePhysicsLayersAsset(std::span<const std::uint8_t>{});
    kb::tests::Require(!corrupt.succeeded, "LIB-129 physics layers asset decode must honestly fail on empty/corrupt input");

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb_engine_physics_scene_tests_lib129";
    std::error_code resetError;
    std::filesystem::remove_all(root, resetError);
    std::filesystem::create_directories(root / "Assets" / "Physics", resetError);
    kb::tests::Require(!resetError, "LIB-129 physics layers asset test project root could not be prepared");
    const std::filesystem::path assetPath = root / "Assets" / "Physics" / "Layers.21kbphysicslayers";
    kb::tests::Require(kb::scene::WritePhysicsLayersAsset(assetPath, asset), "LIB-129 physics layers asset must write to disk");
    const kb::scene::PhysicsLayersAssetLoadResult reread = kb::scene::ReadPhysicsLayersAsset(assetPath);
    kb::tests::Require(reread.succeeded && reread.asset.layerNames[3] == "Enemy", "LIB-129 physics layers asset must read back what it just wrote to disk");

    kb::scene::Scene loaderScene;
    kb::tests::Require(loaderScene.Assets().MountProject(root), "LIB-129 physics layers asset loader test project mount failed");
    kb::tests::Require(loaderScene.Assets().Discover() == 1U, "LIB-129 physics layers asset loader test did not discover exactly the layers asset");
    const kb::assets::AssetHandle<kb::scene::PhysicsLayersAsset> loaded = loaderScene.Assets().Manager().Load<kb::scene::PhysicsLayersAsset>(std::filesystem::path{ "/Game/Physics/Layers.21kbphysicslayers" });
    kb::tests::Require(loaded.IsLoaded(), "LIB-129 PhysicsLayersAssetLoader must be registered and resolve the asset by virtual path");
    kb::tests::Require(loaded->layerNames[3] == "Enemy" && !loaded->LayersInteract(3, 4), "LIB-129 PhysicsLayersAssetLoader must load the real file contents through the AssetManager");
}

} // namespace

namespace kb::tests {

void RunPhysicsSceneSystemTests() {
    RunPhysicsLayersAssetIOTest();
    RunPhysicsSceneSystemFallingBodyTest();
}

} // namespace kb::tests
