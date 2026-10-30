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
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneTimers.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/TimelineAssetIO.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAnimatorComponents.hpp"
#include "engine/scene/SceneTimelines.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#if defined(_WIN32)
#include <Windows.h>
#include <Psapi.h>
#endif

namespace {
kb::project::ProjectDescriptor Descriptor(bool enableInput = false) {
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back({.name = "Physics.Jolt", .binaryPath = KB_PHYSICS_JOLT_PLUGIN_PATH, .enabled = true});
    if (enableInput) descriptor.plugins.push_back({.name = "Input", .enabled = true});
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

std::size_t PrivateBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    kb::tests::Require(K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
        sizeof(counters)) != FALSE, "Process memory measurement failed");
    return counters.PrivateUsage;
#else
    return 0U;
#endif
}

struct HistoryTag {};
struct FutureTag {};
struct HistoryRelation {};
struct FutureComponent { int value = 0; };
template<int Index> struct SideComponent { int value = 0; };
struct SparseMarker {};
class UnknownStateSystem final : public kb::scene::SceneSystem {};

class FaultStateSystem final : public kb::scene::SceneSystem, public kb::core::IRuntimeStateProvider {
    struct State final : kb::core::RuntimeStateSnapshot {
        const FaultStateSystem* owner = nullptr;
        int value = 0;
        std::size_t PayloadBytes() const noexcept override { return sizeof(value); }
    };
public:
    int value = 0;
    bool failNextRestore = false;
    std::function<void()> captureProbe;
    std::function<void()> restoreProbe;
    void OnUpdate(kb::scene::SceneSystemContext&) override { ++value; }
    std::unique_ptr<kb::core::RuntimeStateSnapshot> CaptureRuntimeState(std::string& error) override {
        if(captureProbe) captureProbe();
        auto state=std::make_unique<State>();state->owner=this;state->value=value;error.clear();return state;
    }
    bool ValidateRuntimeState(const kb::core::RuntimeStateSnapshot& snapshot,std::string& error) const override {
        const auto* state=dynamic_cast<const State*>(&snapshot);
        if(!state || state->owner!=this) {error="Wrong fault provider lifetime";return false;}
        error.clear();return true;
    }
    bool RestoreRuntimeState(const kb::core::RuntimeStateSnapshot& snapshot,std::string& error) override {
        if(restoreProbe) restoreProbe();
        if(!ValidateRuntimeState(snapshot,error)) return false;
        if(failNextRestore) {value=-999;failNextRestore=false;error="Injected restore failure";return false;}
        value=static_cast<const State&>(snapshot).value;return true;
    }
};

#include "SceneAnimationStateFixture.inl"

void RunSceneContinuation(bool pipelined, bool useDelta = false) {
    const auto root = std::filesystem::temp_directory_path() / ("kb-scene-state-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "Assets");
    {
        std::ofstream file{root / "Assets/State.lua"};
        file << R"(
local count = 0
local read, write
function Created(self)
    read, write = GetShared, SetShared
    write("created", (read("created") or 0) + 1)
    Events.Subscribe("Pulse", function(e) write("total", (read("total") or 0) + e.args.amount) end)
end
function FixedTick(self, dt) write("fixed", (read("fixed") or 0) + 1) end
function TimerFired(self, event) write("timers", (read("timers") or 0) + 1) end
function OnCollisionEnter(self, event) write("collisions", (read("collisions") or 0) + 1) end
function Tick(self, dt)
    while true do
        count = count + 1
        write("count", count)
        write("random", math.random(1000000))
        Events.EmitDeferred("Pulse", {amount=count})
        coroutine.yield()
    end
end
)";
    }
    std::unique_ptr<kb::core::RuntimeStateSnapshot> detached;
    {
        SceneAnimationStateFixture::Write(root);
        kb::scene::Scene scene{Descriptor(true)};
        kb::tests::Require(scene.IsModuleActive("Physics.Jolt"), "Scene continuation requires the real physics module");
        kb::tests::Require(scene.IsModuleActive("Input"), "Scene continuation requires the built-in input module");
        kb::scene::PhysicsBackend::SetStepPipelining(scene, pipelined);
        kb::tests::Require(scene.Assets().MountProject(root) && scene.Assets().Discover()==5U, "Scene script fixture mount failed");
        const auto* script=scene.Assets().Manager().Registry().FindByPath("/Game/State.lua");
        kb::tests::Require(script!=nullptr, "Scene script fixture missing");
        SceneAnimationStateFixture animation{scene};
        const auto floor=scene.Entities().CreateObject({.name="Floor", .transform={.localPosition={0,-0.5F,0}}});
        scene.Components().Colliders().Set(floor.Entity(), {.boxSize={40,1,40}});
        const auto body=scene.Entities().CreateObject({.name="Body", .transform={.localPosition={0,3,0}}});
        scene.Components().Colliders().Set(body.Entity(), {.restitution=0.35F});
        scene.Components().Rigidbodies().Set(body.Entity(), {.bodyType=kb::scene::RigidbodyBodyType::Dynamic, .linearVelocity={1,-1,0}});
        scene.Components().Behaviours().Set(body.Entity(), {.behaviourAssetId=script->id.value, .backend=kb::scene::BehaviourBackend::Lua});
        const auto parent=scene.Entities().CreateObject({.name="Parent"});
        const auto secondParent=scene.Entities().CreateObject({.name="SecondParent"});
        const auto child=scene.Entities().CreateObject({.name="Child"});
        const auto secondChild=scene.Entities().CreateObject({.name="SecondChild"});
        kb::tests::Require(child.SetParent(parent) && secondChild.SetParent(secondParent), "Scene fixture parent setup failed");
        auto& world=scene.Runtime().EcsWorld();
        world.SetName(child.EntityHandle(), "Repeated");
        world.SetName(secondChild.EntityHandle(), "Repeated");
        const auto tag=world.RegisterTag<HistoryTag>("HistoryTag");
        const auto futureTag=world.RegisterTag<FutureTag>("FutureTag");
        const auto relation=world.RegisterRelation<HistoryRelation>("HistoryRelation");
        world.AddTag(child.EntityHandle(), tag);
        world.AddRelation(child.EntityHandle(), relation, body.EntityHandle());
        static_cast<void>(world.RegisterComponent<SideComponent<0>>("ColdState", {.storageClass=kb::ecs::ComponentStorageClass::ColdTable}));
        static_cast<void>(world.RegisterComponent<SideComponent<1>>("SparseState", {.storageClass=kb::ecs::ComponentStorageClass::SparsePayload}));
        static_cast<void>(world.RegisterComponent<SideComponent<2>>("SharedState", {.storageClass=kb::ecs::ComponentStorageClass::SharedValue}));
        static_cast<void>(world.RegisterComponent<SideComponent<3>>("BlobState", {.storageClass=kb::ecs::ComponentStorageClass::ExternalBlob}));
        static_cast<void>(world.RegisterComponent<SparseMarker>("SparseMarker", {.storageClass=kb::ecs::ComponentStorageClass::SparseTag}));
        world.Set(child.EntityHandle(),SideComponent<0>{10});
        world.Set(child.EntityHandle(),SideComponent<1>{20});
        world.Set(child.EntityHandle(),SideComponent<2>{30});
        world.Set(child.EntityHandle(),SideComponent<3>{40});
        world.Set(child.EntityHandle(),SparseMarker{});
        scene.Entities().SetActive(secondChild, false);
        scene.Transforms().SetLocalTranslation(child.Entity(), {100000000.25, 0, 0});
        kb::script::ScriptRuntimeHost host{scene};
        kb::tests::Require(host.Succeeded() && host.InstallSceneSystem(), "Scene host installation failed");
        auto faultOwner=std::make_unique<FaultStateSystem>();
        auto* fault=faultOwner.get();
        const auto faultHandle=scene.Runtime().AddSceneSystem(std::move(faultOwner));
        bool captureGuarded=false;
        bool restoreGuarded=false;
        fault->captureProbe=[&] {
            try { static_cast<void>(scene.Runtime().AddSceneSystem(std::make_unique<UnknownStateSystem>())); }
            catch(const std::logic_error&) { captureGuarded=true; }
            std::string nestedError;
            kb::tests::Require(!scene.Runtime().CaptureRuntimeState(nestedError) && !nestedError.empty(),
                "Provider capture reentered the scene checkpoint");
        };
        fault->restoreProbe=[&] {
            restoreGuarded=!scene.Runtime().RemoveSceneSystem(faultHandle);
            kb::tests::Require(restoreGuarded,"Provider restore removed its own active system");
        };
        const auto timer=scene.Timers().Repeat(0.037F, body.Entity());
        kb::tests::Require(timer!=0U, "Scene timer fixture failed");
        auto tick=[&](int index) {
            const float dt=index%3==0 ? 0.011F : 0.019F;
            kb::tests::Require(scene.Runtime().Update(dt), "Scene continuation update failed");
            const auto* result=host.InstalledSceneSystemLastResult();
            const auto* prepared=host.InstalledSceneSystemLastPrepareResult();
            if (result && !result->Succeeded()) for (const auto& d:result->diagnostics) std::cerr<<d.message<<'\n';
            kb::tests::Require(result && result->Succeeded() && prepared && prepared->Succeeded(), "Scene script continuation failed");
        };
        for(int i=0;i<20;++i) tick(i);
        animation.BeginTransition();
        scene.Input().MutableDeviceState().SetKeyDown(kb::input::InputKey::W,true);
        kb::tests::Require(scene.Runtime().EnqueueCommand({.kind=kb::scene::SceneRuntimeCommandKind::SetTimeScale, .timeScale=0.75F}), "Queued source command failed");
        std::string error;
        kb::scene::SceneObject baseOnly;
        kb::scene::SceneObject deltaOnly;
        if(useDelta) baseOnly=scene.Entities().CreateObject({.name="BaseOnly"});
        auto snapshot=scene.Runtime().CaptureRuntimeState(error);
        if(!snapshot) std::cerr<<"Scene capture: "<<error<<'\n';
        kb::tests::Require(snapshot!=nullptr, "Coordinated scene capture failed");
        kb::tests::Require(captureGuarded,"Provider capture changed scheduler topology");
        if(useDelta) {
            scene.Entities().Destroy(baseOnly);
            deltaOnly=scene.Entities().CreateObject({.name="DeltaOnly"});
            for(int i=0;i<3;++i) tick(i);
            auto delta=scene.Runtime().CaptureRuntimeDeltaState(*snapshot,error);
            kb::tests::Require(delta!=nullptr,"Coordinated scene delta capture failed");
            snapshot=std::move(delta); // The native baseline must outlive its original provider image.
            kb::tests::Require(!scene.Runtime().CaptureRuntimeDeltaState(*snapshot,error) && !error.empty(),
                "Scene accepted a delta as a full baseline");
        }
        struct Sample {
            std::array<double,12> simulation;
            std::array<std::int64_t,7> script;
            SceneAnimationStateFixture::Sample animation;
        };
        auto sample=[&] {
            const auto position=scene.Transforms().Get(body).localPosition;
            const auto velocity=kb::scene::PhysicsBackend::GetVelocity(scene,body.Entity());
            const auto interpolated=scene.Runtime().InterpolatedTransform(body.Entity());
            kb::tests::Require(velocity.found && interpolated.has_value(), "Scene sample lacks physics/interpolation state");
            auto value=[&](const char* key) {const auto v=host.SharedState().Get(key);return v?v->AsInt():0;};
            return Sample{{position.x,position.y,position.z,velocity.value.x,velocity.value.y,
                scene.Runtime().ElapsedSeconds(),static_cast<double>(scene.Runtime().FrameIndex()),
                static_cast<double>(scene.Runtime().FixedStepIndex()),scene.Runtime().FixedInterpolationAlpha(),
                interpolated->localPosition.y,scene.Transforms().LocalTranslation(child.Entity()).x,scene.Runtime().TimeScale()},
                {value("created"),value("count"),value("random"),value("total"),value("fixed"),value("timers"),value("collisions")}, animation.Read()};
        };
        std::vector<Sample> expected;
        for(int i=0;i<60;++i) {tick(i);expected.push_back(sample());}
        const auto beforeFailure=sample();const auto providerBeforeFailure=fault->value;
        fault->failNextRestore=true;
        kb::tests::Require(!scene.Runtime().RestoreRuntimeState(*snapshot,error)
            && error.find("previous state restored")!=std::string::npos,"Provider restore failure did not roll back");
        const auto afterFailure=sample();
        kb::tests::Require(afterFailure.simulation==beforeFailure.simulation && afterFailure.script==beforeFailure.script
            && afterFailure.animation==beforeFailure.animation && fault->value==providerBeforeFailure,"Failed checkpoint restore did not preserve the prior scene/provider state");
        animation.DestroyFuture();
        scene.Entities().Destroy(body);
        const auto futureParent=scene.Entities().CreateObject({.name="FutureParent"});
        kb::tests::Require(child.SetParent(futureParent), "Future parent setup failed");
        scene.Entities().SetName(child,"FutureName");
        scene.Entities().SetActive(secondChild,true);
        scene.Transforms().SetLocalTranslation(child.Entity(), {2,3,4});
        world.RemoveTag(child.EntityHandle(),tag);
        world.AddTag(child.EntityHandle(),futureTag);
        world.AddRelation(child.EntityHandle(),relation,floor.EntityHandle());
        static_cast<void>(world.RegisterComponent<FutureComponent>("FutureComponent"));
        world.Set(child.EntityHandle(),FutureComponent{42});
        world.Set(child.EntityHandle(),SideComponent<0>{101});
        world.Set(child.EntityHandle(),SideComponent<1>{102});
        world.Set(child.EntityHandle(),SideComponent<2>{103});
        world.Set(child.EntityHandle(),SideComponent<3>{104});
        world.Remove<SparseMarker>(child.EntityHandle());
        scene.Input().MutableDeviceState().SetKeyDown(kb::input::InputKey::W,false);
        scene.Components().Colliders().Remove(floor.Entity());
        const auto extra=scene.Entities().CreateObject({.name="FutureBody"});
        scene.Components().Colliders().Set(extra.Entity(),{});
        tick(1);
        kb::tests::Require(scene.Runtime().EnqueueCommand({.kind=kb::scene::SceneRuntimeCommandKind::RequestQuit}), "Queued future command failed");
        const auto started=std::chrono::steady_clock::now();
        const bool restored=scene.Runtime().RestoreRuntimeState(*snapshot,error);
        const auto restoreUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();
        if(!restored) std::cerr<<"Scene restore: "<<error<<'\n';
        kb::tests::Require(restored, "Coordinated scene restore failed");
        kb::tests::Require(restoreGuarded,"Provider restore did not hold scheduler ownership");
        if(useDelta) kb::tests::Require(!scene.Entities().IsAlive(baseOnly) && scene.Entities().IsAlive(deltaOnly),
            "Scene delta did not restore structural identity changes");
        kb::tests::Require(scene.Input().DeviceState().IsKeyDown(kb::input::InputKey::W), "Scene-owned device state did not restore");
        kb::tests::Require(scene.Entities().IsAlive(body) && !scene.Entities().IsAlive(futureParent)
            && !scene.Entities().IsAlive(extra) && child.Parent().Entity()==parent.Entity()
            && child.Name()=="Child" && !scene.Entities().IsActive(secondChild)
            && world.Name(child.EntityHandle())=="Repeated" && world.Name(secondChild.EntityHandle())=="Repeated"
            && world.HasTag(child.EntityHandle(),tag) && !world.HasTag(child.EntityHandle(),futureTag)
            && world.HasRelation(child.EntityHandle(),relation,body.EntityHandle())
            && !world.HasRelation(child.EntityHandle(),relation,floor.EntityHandle()) && !world.Has<FutureComponent>(child.EntityHandle()),
            "Scene structural/metadata replacement did not restore the snapshot");
        const auto* cold=world.TryGet<SideComponent<0>>(child.EntityHandle());
        const auto* sparse=world.TryGet<SideComponent<1>>(child.EntityHandle());
        const auto* shared=world.TryGet<SideComponent<2>>(child.EntityHandle());
        const auto* blob=world.TryGet<SideComponent<3>>(child.EntityHandle());
        kb::tests::Require(cold && sparse && shared && blob && cold->value==10
            && sparse->value==20 && shared->value==30 && blob->value==40
            && world.Has<SparseMarker>(child.EntityHandle()),"Native side storage did not restore");
        for(std::size_t i=0;i<expected.size();++i) {
            tick(static_cast<int>(i));const auto actual=sample();
            kb::tests::Require(actual.animation==expected[i].animation,"Animator/timeline continuation differs");
            kb::tests::Require(actual.script==expected[i].script, "Joint Lua event/timer/coroutine replay differs");
            for(std::size_t j=0;j<actual.simulation.size();++j) if(std::abs(actual.simulation[j]-expected[i].simulation[j])>0.0001) {
                std::cerr<<"Scene replay frame="<<i<<" value="<<j<<" expected="<<expected[i].simulation[j]<<" actual="<<actual.simulation[j]<<'\n';
                kb::tests::Require(false,"Joint physics/clock/precision replay differs");
            }
        }
        const auto before=PrivateBytes();
        std::vector<std::unique_ptr<kb::core::RuntimeStateSnapshot>> history;
        auto historyBase=useDelta ? scene.Runtime().CaptureRuntimeState(error) : nullptr;
        kb::tests::Require(!useDelta || historyBase!=nullptr,"Scene history baseline capture failed");
        std::size_t payload=historyBase ? historyBase->PayloadBytes() : 0U;
        for(int i=0;i<32;++i) {
            auto image=useDelta ? scene.Runtime().CaptureRuntimeDeltaState(*historyBase,error) : scene.Runtime().CaptureRuntimeState(error);
            kb::tests::Require(image!=nullptr,"Scene history capture failed");
            if(useDelta) kb::tests::Require(image->PayloadBytes()<historyBase->PayloadBytes(),"Unchanged native scene delta retained full column payload");
            payload+=image->PayloadBytes();history.push_back(std::move(image));
        }
        const auto after=PrivateBytes();history.clear();historyBase.reset();const auto released=PrivateBytes();
        const auto unknown=scene.Runtime().AddSceneSystem(std::make_unique<UnknownStateSystem>());
        kb::tests::Require(!scene.Runtime().CaptureRuntimeState(error) && !error.empty(), "Scene silently skipped a system without state provider");
        kb::tests::Require(!scene.Runtime().RestoreRuntimeState(*snapshot,error), "Scene accepted changed system topology");
        static_cast<void>(scene.Runtime().RemoveSceneSystem(unknown));
        detached=scene.Runtime().CaptureRuntimeState(error);
        kb::tests::Require(detached!=nullptr,"Detached coordinated scene capture failed");
        animation.VerifyChangedAssetRefusal(*detached);
        std::cout<<"Scene continuation pipelined="<<pipelined<<" delta="<<useDelta<<" replay_frames=60 restore_us="<<restoreUs
            <<" history_images=32 retained_full_baselines="<<useDelta<<" measured_payload_bytes="<<payload<<" process_private_before="<<before
            <<" process_private_with_history="<<after<<" process_private_after_release="<<released<<'\n';
    }
    detached.reset();
    std::filesystem::remove_all(root);
}
}

int main() {
    RunContinuation(false);
    RunContinuation(true);
    RunDetachedLifetime();
    RunSceneContinuation(false);
    RunSceneContinuation(true);
    RunSceneContinuation(false,true);
    RunSceneContinuation(true,true);
}
