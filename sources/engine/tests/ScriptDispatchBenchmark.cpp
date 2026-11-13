#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/script/NativeScriptPlugin.hpp"
#include "engine/script/NativeScriptPluginManager.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::tests {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double MillisecondsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

constexpr kb::assets::AssetId kBenchAsset{ 9701U };
constexpr float kFrameSeconds = 1.0F / 60.0F;

constexpr const char* kEmptyScript = "function Tick(self, dt) end\n";

// Reads and writes the transform, reads input and calls the engine, the work of a typical gameplay script.
constexpr const char* kTypicalScript = R"(
local speed = 2.0
function Tick(self, dt)
    local p = Transform.GetPosition(self.entity)
    if p ~= nil then
        if Input.IsPressed("Jump") then speed = -speed end
        Transform.SetPosition(self.entity, p.x + speed * dt, p.y, p.z)
    end
end
)";

// A script whose Tick makes the same call 200 times: the per-call price of one binding.
[[nodiscard]] std::string BindingScript(const char* statement) {
    return std::string{ "function Tick(self, dt)\n  for i = 1, 200 do\n    " } + statement + "\n  end\nend\n";
}

// A scene of `count` scripted objects running one Lua asset, stepped with the real scene system.
struct Rig {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::script::ScriptRuntimeSceneSystem system{ host.Runtime() };
    std::vector<kb::scene::SceneEntity> entities;

    Rig(std::size_t count, const char* source, bool exposeVariable = false) {
        Require(host.Succeeded(), "script benchmark host did not initialize");
        Require(host.LuaRuntime().LoadScript(kBenchAsset, source, "Bench.lua").succeeded, "script benchmark script did not load");
        if (exposeVariable) {
            const std::vector<kb::script::ScriptApiPin> pins{ kb::script::ScriptApiPin{ .name = "speed", .type = kb::script::ScriptValueType::Float } };
            const std::vector<kb::script::ScriptValue> defaults{ kb::script::ScriptValue{ 2.0F } };
            const std::vector<std::uint8_t> hasDefaults{ 1U };
            host.LuaRuntime().SetScriptExposedVariables(kBenchAsset, pins, defaults, hasDefaults);
        }
        std::vector<kb::scene::SceneObjectDesc> descs(count);
        for (std::size_t index = 0U; index < count; ++index) {
            descs[index].transform.localPosition = kb::scene::Vec3{ static_cast<float>(index % 100U), 0.0F, static_cast<float>(index / 100U) };
        }
        for (const kb::scene::SceneObject& object : scene.Entities().CreateObjects(descs)) {
            entities.push_back(object.Entity());
            scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
                .behaviourAssetId = kBenchAsset.value, .backend = kb::scene::BehaviourBackend::Lua, .enabled = true });
        }
    }

    // One frame as the installed scene system runs it: the scripts, then the collector's end-of-frame step.
    void Frame() {
        static_cast<void>(system.ExecuteFrame(scene, kFrameSeconds));
        host.LuaRuntime().StepGarbageCollector();
        Require(system.LastResult().Succeeded(), system.LastResult().diagnostics.empty() ? "script benchmark frame failed" : system.LastResult().diagnostics.front().message.c_str());
    }
};

struct FrameStats {
    double median = 0.0;
    double p99 = 0.0;
    double maximum = 0.0;
    double cppAllocations = 0.0; // per frame, heap blocks the engine's own code asked for
    double luaAllocations = 0.0; // per frame, blocks the Lua state asked for
};

[[nodiscard]] FrameStats MeasureFrames(Rig& rig, int frames) {
    for (int warmup = 0; warmup < 3; ++warmup) rig.Frame();
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(frames));
    const std::uint64_t luaBefore = rig.host.LuaRuntime().LuaAllocationCount();
    BeginAllocationTally();
    for (int frame = 0; frame < frames; ++frame) {
        const Clock::time_point start = Clock::now();
        rig.Frame();
        times.push_back(MillisecondsSince(start));
    }
    const AllocationTally tally = EndAllocationTally();
    FrameStats stats;
    stats.cppAllocations = static_cast<double>(tally.count) / frames;
    stats.luaAllocations = static_cast<double>(rig.host.LuaRuntime().LuaAllocationCount() - luaBefore) / frames;
    std::sort(times.begin(), times.end());
    stats.median = times[times.size() / 2U];
    stats.p99 = times[std::min(times.size() - 1U, static_cast<std::size_t>(static_cast<double>(times.size()) * 0.99))];
    stats.maximum = times.back();
    return stats;
}

void ReportFrames(const char* label, std::size_t count, const FrameStats& stats) {
    std::printf("script-bench %-22s N=%-6zu frame median %8.3f ms  p99 %8.3f ms  max %8.3f ms  per-entity %7.3f us  c++ allocs/frame %9.1f  lua allocs/frame %9.1f\n",
        label, count, stats.median, stats.p99, stats.maximum, stats.median * 1000.0 / static_cast<double>(count), stats.cppAllocations, stats.luaAllocations);
}

// Nanoseconds one call of `statement` costs inside a running Tick (200 calls per entity per frame).
[[nodiscard]] double BindingNanoseconds(const char* statement, bool& succeeded) {
    Rig rig(50U, BindingScript(statement).c_str());
    for (int warmup = 0; warmup < 3; ++warmup) rig.Frame();
    const FrameStats loop = MeasureFrames(rig, 40);
    Rig empty(50U, BindingScript("local _ = i").c_str());
    const FrameStats base = MeasureFrames(empty, 40);
    succeeded = rig.system.LastResult().Succeeded();
    return std::max(0.0, (loop.median - base.median) * 1.0e6 / (50.0 * 200.0));
}

// ---- tests ---------------------------------------------------------------------------------------------------

struct SmallRig {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::script::ScriptRuntimeSceneSystem system{ host.Runtime() };

    explicit SmallRig(const char* source) {
        Require(host.Succeeded(), "script dispatch test host did not initialize");
        const kb::script::PucLuaLoadResult loaded = host.LuaRuntime().LoadScript(kBenchAsset, source, "Test.lua");
        Require(loaded.succeeded, loaded.error.c_str());
    }

    kb::scene::SceneEntity Add(kb::scene::BehaviourTickGroup group = kb::scene::BehaviourTickGroup::Gameplay, std::int32_t order = 0, bool enabled = true) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Scripted" });
        scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
            .behaviourAssetId = kBenchAsset.value, .backend = kb::scene::BehaviourBackend::Lua, .enabled = enabled,
            .tickGroup = group, .executionOrder = order });
        return object.Entity();
    }

    const kb::script::ScriptRuntimeExecutionResult& Frame() {
        const kb::script::ScriptRuntimeExecutionResult& result = system.ExecuteFrame(scene, kFrameSeconds);
        Require(result.Succeeded(), result.diagnostics.empty() ? "script dispatch test frame failed" : result.diagnostics.front().message.c_str());
        return result;
    }
};

[[nodiscard]] std::vector<std::uint64_t> SendersOf(const kb::script::ScriptRuntimeExecutionResult& result, std::string_view eventName) {
    std::vector<std::uint64_t> senders;
    for (const kb::script::ScriptEvent& event : result.emittedEvents) {
        if (event.name == eventName) senders.push_back(event.sender.Id());
    }
    return senders;
}

// A number, integer or boolean argument of an event as a float; -1000 when it has none of those types.
[[nodiscard]] float ArgumentOf(const kb::script::ScriptEvent& event, std::string_view name) {
    for (const kb::script::ScriptEventArgument& argument : event.arguments) {
        if (argument.name != name) continue;
        const kb::script::ScriptValue& value = argument.value;
        switch (value.Type()) {
        case kb::script::ScriptValueType::Bool: return value.AsBool() ? 1.0F : 0.0F;
        case kb::script::ScriptValueType::Int: return static_cast<float>(value.AsInt());
        case kb::script::ScriptValueType::Int64: return static_cast<float>(value.AsInt64());
        case kb::script::ScriptValueType::Float: return value.AsFloat();
        case kb::script::ScriptValueType::Double: return static_cast<float>(value.AsDouble());
        default: break;
        }
    }
    return -1000.0F;
}

struct Expected {
    kb::scene::SceneEntity entity;
    kb::scene::BehaviourTickGroup group;
    std::int32_t order;
};

[[nodiscard]] std::vector<std::uint64_t> ExpectedOrder(std::vector<Expected> behaviours) {
    std::sort(behaviours.begin(), behaviours.end(), [](const Expected& lhs, const Expected& rhs) {
        if (lhs.group != rhs.group) return static_cast<int>(lhs.group) < static_cast<int>(rhs.group);
        if (lhs.order != rhs.order) return lhs.order < rhs.order;
        return lhs.entity.Id() < rhs.entity.Id();
    });
    std::vector<std::uint64_t> ids;
    for (const Expected& behaviour : behaviours) ids.push_back(behaviour.entity.Id());
    return ids;
}

// Behaviours run by tick group, execution order and entity id, frame after frame, and the cached order follows the
// scene when behaviours are added, disabled and destroyed.
void RunDispatchOrderIsDeterministicTest() {
    SmallRig rig("function Tick(self, dt) Emit('Visited') end\n");
    const kb::scene::BehaviourTickGroup groups[] = { kb::scene::BehaviourTickGroup::Presentation, kb::scene::BehaviourTickGroup::Input,
        kb::scene::BehaviourTickGroup::Gameplay, kb::scene::BehaviourTickGroup::Physics };
    std::vector<Expected> behaviours;
    for (int index = 0; index < 24; ++index) {
        const auto group = groups[(index * 5) % 4];
        const std::int32_t order = (7 - index) % 3;
        behaviours.push_back({ rig.Add(group, order), group, order });
    }
    for (int frame = 0; frame < 3; ++frame) {
        Require(SendersOf(rig.Frame(), "Visited") == ExpectedOrder(behaviours), "Behaviours must run by tick group, execution order and entity id on every frame");
    }

    const kb::scene::BehaviourTickGroup firstGroup = kb::scene::BehaviourTickGroup::Input;
    behaviours.push_back({ rig.Add(firstGroup, -5), firstGroup, -5 });
    rig.scene.Entities().Destroy(behaviours[3].entity);
    behaviours.erase(behaviours.begin() + 3);
    kb::scene::BehaviourComponent disabled = *rig.scene.Components().Behaviours().TryGet(behaviours[5].entity);
    disabled.enabled = false;
    rig.scene.Components().Behaviours().Set(behaviours[5].entity, disabled);
    behaviours.erase(behaviours.begin() + 5);
    kb::scene::BehaviourComponent moved = *rig.scene.Components().Behaviours().TryGet(behaviours[7].entity);
    moved.executionOrder = -9;
    rig.scene.Components().Behaviours().Set(behaviours[7].entity, moved);
    behaviours[7].order = -9;
    for (int frame = 0; frame < 2; ++frame) {
        Require(SendersOf(rig.Frame(), "Visited") == ExpectedOrder(behaviours), "The execution order must follow behaviours that were added, disabled, destroyed or re-ordered");
    }
}

// A frame of scripts makes no engine heap allocation per behaviour and few Lua ones.
void RunDispatchDoesNotAllocateInTheHotPathTest() {
    double cppPerFrame[2] = {};
    double luaPerBehaviour = 0.0;
    const std::size_t counts[2] = { 300U, 900U };
    for (int index = 0; index < 2; ++index) {
        Rig rig(counts[index], kEmptyScript);
        for (int warmup = 0; warmup < 5; ++warmup) rig.Frame();
        const std::uint64_t luaBefore = rig.host.LuaRuntime().LuaAllocationCount();
        BeginAllocationTally();
        for (int frame = 0; frame < 10; ++frame) rig.Frame();
        const AllocationTally tally = EndAllocationTally();
        cppPerFrame[index] = static_cast<double>(tally.count) / 10.0;
        luaPerBehaviour = static_cast<double>(rig.host.LuaRuntime().LuaAllocationCount() - luaBefore) / 10.0 / static_cast<double>(counts[index]);
    }
    std::printf("script-dispatch c++ allocations per frame: %.1f for 300 behaviours, %.1f for 900; lua allocations per behaviour call: %.2f\n",
        cppPerFrame[0], cppPerFrame[1], luaPerBehaviour);
    // 600 more behaviours: any allocation per behaviour would show as at least 600 more per frame.
    Require(cppPerFrame[1] - cppPerFrame[0] <= 8.0, "Calling a Lua callback must not allocate engine memory per behaviour");
    Require(cppPerFrame[1] <= 64.0, "A frame of empty Lua callbacks must not allocate engine memory per frame beyond a few buffers");
    Require(luaPerBehaviour <= 4.0, "A Lua callback must build no more than its self table, its variables table and the argument values");
}

// The collector's end-of-frame step stays inside its budget and the heap does not grow while scripts run.
void RunCollectorStepStaysWithinBudgetTest() {
    Rig rig(2000U, kTypicalScript);
    kb::script::PucLuaScriptRuntime& lua = rig.host.LuaRuntime();
    const std::uint32_t budget = kb::script::ScriptExecutionBudgetSettings{}.luaCollectionMicrosecondsPerFrame;
    Require(budget > 0U, "The default script budget must set a collector step budget");
    std::size_t earlyHeap = 0U;
    constexpr int kFrames = 120;
    for (int frame = 0; frame < kFrames; ++frame) {
        rig.Frame();
        if (frame == 30) earlyHeap = lua.LuaMemoryOwnedBytes();
    }
    const kb::script::PucLuaCollectionStats stats = lua.CollectionStats();
    std::printf("script-dispatch collector: %llu steps, average %.1f us, longest %llu us (budget %u us); heap %zu -> %zu bytes\n",
        static_cast<unsigned long long>(stats.steps), static_cast<double>(stats.totalMicroseconds) / static_cast<double>(stats.steps),
        static_cast<unsigned long long>(stats.longestStepMicroseconds), budget, earlyHeap, lua.LuaMemoryOwnedBytes());
    Require(stats.steps == static_cast<std::uint64_t>(kFrames), "Every frame must run one collector step");
    Require(static_cast<double>(stats.totalMicroseconds) / static_cast<double>(stats.steps) <= static_cast<double>(budget), "A collector step must stay within the per-frame budget on average");
    Require(stats.longestStepMicroseconds <= std::uint64_t{ budget } * 8U, "No collector step may take many times the per-frame budget");
    Require(lua.LuaMemoryOwnedBytes() <= earlyHeap + earlyHeap / 2U + (256U << 10U), "The script heap must not keep growing across frames");

    kb::script::ScriptExecutionBudgetSettings off;
    off.luaCollectionMicrosecondsPerFrame = 0U;
    lua.SetExecutionBudgetSettings(off);
    lua.StepGarbageCollector();
    Require(lua.CollectionStats().steps == stats.steps, "A zero budget must leave the collector to the scripts' own allocations");
}

// A reloaded script runs its new code on the live instances, which keep their exposed fields.
void RunHotReloadKeepsFieldsTest() {
    SmallRig rig("function Tick(self, dt) Emit('Seen', { speed = Inspector.speed, version = 1 }) end\n");
    const std::vector<kb::script::ScriptApiPin> pins{ kb::script::ScriptApiPin{ .name = "speed", .type = kb::script::ScriptValueType::Float } };
    const std::vector<kb::script::ScriptValue> defaults{ kb::script::ScriptValue{ 2.0F } };
    const std::vector<std::uint8_t> hasDefaults{ 1U };
    rig.host.LuaRuntime().SetScriptExposedVariables(kBenchAsset, pins, defaults, hasDefaults);
    std::vector<kb::scene::SceneEntity> entities;
    for (int index = 0; index < 3; ++index) entities.push_back(rig.Add());
    rig.host.LuaRuntime().SetInstanceVariableOverride(entities[1], kBenchAsset, "speed", kb::script::ScriptValue{ 9.5F });

    const auto speedsAndVersion = [&](const kb::script::ScriptRuntimeExecutionResult& result, float expectedVersion) {
        std::vector<float> speeds;
        for (const kb::script::ScriptEvent& event : result.emittedEvents) {
            if (event.name != "Seen") continue;
            Require(ArgumentOf(event, "version") == expectedVersion, "The frame after a reload must run the new code");
            speeds.push_back(ArgumentOf(event, "speed"));
        }
        return speeds;
    };
    const std::vector<float> before = speedsAndVersion(rig.Frame(), 1.0F);
    Require(before == std::vector<float>({ 2.0F, 9.5F, 2.0F }), "Exposed fields must read their default or their override");

    const Clock::time_point start = Clock::now();
    const kb::script::PucLuaLoadResult reloaded = rig.host.LuaRuntime().ReloadScript(kBenchAsset,
        "function Tick(self, dt) Emit('Seen', { speed = Inspector.speed, version = 2 }) end\n", "Test.lua");
    Require(reloaded.succeeded, "The edited script must reload");
    Require(MillisecondsSince(start) < 250.0, "Reloading a small script must take well under a frame");
    const std::vector<float> after = speedsAndVersion(rig.Frame(), 2.0F);
    Require(after == before, "A hot reload must keep the exposed fields of the live instances");
    Require(!rig.host.LuaRuntime().InstanceVariables(entities[1], kBenchAsset).empty() &&
            rig.host.LuaRuntime().InstanceVariables(entities[1], kBenchAsset).front().overridden,
        "A hot reload must keep an override");

    // The reloaded source decides what a phase runs: with no Tick left, the instances are passed over.
    const kb::scene::BehaviourComponent behaviour = *rig.scene.Components().Behaviours().TryGet(entities[0]);
    Require(rig.host.LuaRuntime().HasEntry(behaviour, "Tick"), "The reloaded script defines Tick");
    Require(rig.host.LuaRuntime().ReloadScript(kBenchAsset, "function LateTick(self, dt) Emit('Late') end\n", "Test.lua").succeeded, "The second edit must reload");
    Require(!rig.host.LuaRuntime().HasEntry(behaviour, "Tick"), "A reloaded script without Tick must be passed over in the Tick phase");
    Require(SendersOf(rig.Frame(), "Late").size() == 3U, "A reloaded script must run the callbacks it now defines");
}

// Instances whose script defines no callback for a phase cost nothing there, and a callback a script defines while it
// runs is still called.
void RunDispatchSkipsMissingCallbacksTest() {
    SmallRig rig("function LateTick(self, dt) Emit('Late') end\n");
    for (int index = 0; index < 20; ++index) rig.Add();
    const kb::script::ScriptRuntimeExecutionResult& first = rig.Frame();
    Require(SendersOf(first, "Late").size() == 20U && first.executedBehaviours == 20U, "Only the phase the script defines must run");
    Require(first.visitedBehaviours >= 20U * 5U, "Passed-over behaviours still count as visited");

    SmallRig dynamic("function Created(self) Tick = function(self, dt) Emit('Dynamic') end end\n");
    for (int index = 0; index < 5; ++index) dynamic.Add();
    Require(SendersOf(dynamic.Frame(), "Dynamic").size() == 5U, "A Tick a script defines in Created must be called from the first frame on");
}

// An event for one entity reaches that entity's script only.
void RunTargetedEventReachesOneBehaviourTest() {
    SmallRig rig("function Ping(self, event) Emit('Pong') end\n");
    std::vector<kb::scene::SceneEntity> entities;
    for (int index = 0; index < 30; ++index) entities.push_back(rig.Add());
    static_cast<void>(rig.Frame());
    kb::script::ScriptEvent event;
    event.name = "Ping";
    event.target = entities[17];
    kb::script::ScriptRuntimeExecutionResult targeted = rig.host.Runtime().DispatchEvent(rig.scene, event, 0.0F);
    Require(targeted.executedBehaviours == 1U && SendersOf(targeted, "Pong") == std::vector<std::uint64_t>{ entities[17].Id() }, "A targeted event must run the target's handler only");
    event.target = kb::scene::SceneEntity{};
    Require(rig.host.Runtime().DispatchEvent(rig.scene, event, 0.0F).executedBehaviours == 30U, "An event without a target must reach every behaviour");
    rig.scene.Entities().Destroy(entities[17]);
    event.target = entities[17];
    Require(rig.host.Runtime().DispatchEvent(rig.scene, event, 0.0F).executedBehaviours == 0U, "An event for a destroyed entity reaches nothing");
}

// Entries that yield keep their own state while the finished ones hand their threads on.
void RunYieldingEntriesShareThreadsSafelyTest() {
    SmallRig rig("function Tick(self, dt)\n  coroutine.yield()\n  Emit('Resumed')\nend\n");
    for (int index = 0; index < 40; ++index) rig.Add();
    Require(SendersOf(rig.Frame(), "Resumed").empty(), "Every Tick yields on its first call");
    Require(rig.host.LuaRuntime().SuspendedCoroutineCount() == 40U, "Every yielded entry keeps its own coroutine");
    Require(SendersOf(rig.Frame(), "Resumed").size() == 40U, "Every yielded entry resumes where it stopped");
    Require(rig.host.LuaRuntime().SuspendedCoroutineCount() == 0U, "A finished entry releases its coroutine");
    Require(SendersOf(rig.Frame(), "Resumed").empty() && rig.host.LuaRuntime().SuspendedCoroutineCount() == 40U, "A finished entry starts again on a pooled thread");
}

// The direct Transform bindings keep the semantics of the registered functions.
void RunTransformBindingsTest() {
    SmallRig rig(R"(
function Tick(self, dt)
  local ok1 = Transform.SetPosition(self.entity, 1, 2, 3)
  local ok2 = Transform.Translate({ entity = self.entity, x = 0.5 })
  local p = Transform.GetPosition(self.entity)
  local ok3 = Transform.SetPosition({ entity = self.entity, y = 7 })
  local q = Transform.GetPosition(self.entity)
  local missing = Transform.GetPosition(999999999)
  local bad = Transform.SetPosition({ entity = self.entity, x = 'far' })
  local ok4 = Transform.Translate(self.entity, 1, 1, 1)
  local r = Transform.GetPosition(self.entity)
  local sx, sy, sz = Transform.GetPositionXYZ(self.entity)
  local gone = Transform.GetPositionXYZ(999999999)
  Emit('Done', { ok = ok1 and ok2 and ok3 and ok4 and not bad, px = p.x, py = p.y, pz = p.z, qx = q.x, qy = q.y, qz = q.z,
                 rx = r.x, missing = missing == nil and gone == nil, pressed = Input.IsPressed('NoSuchAction'),
                 same = sx == r.x and sy == r.y and sz == r.z })
end
)");
    const kb::scene::SceneEntity entity = rig.Add();
    const kb::script::ScriptRuntimeExecutionResult& result = rig.Frame();
    const kb::script::ScriptEvent* done = nullptr;
    for (const kb::script::ScriptEvent& event : result.emittedEvents) {
        if (event.name == "Done") done = &event;
    }
    Require(done != nullptr, "The transform script must finish");
    Require(ArgumentOf(*done, "ok") == 1.0F && ArgumentOf(*done, "missing") == 1.0F && ArgumentOf(*done, "pressed") == 0.0F, "Transform bindings must report success, absence and invalid arguments");
    Require(NearlyEqual(ArgumentOf(*done, "px"), 1.5F) && NearlyEqual(ArgumentOf(*done, "py"), 2.0F) && NearlyEqual(ArgumentOf(*done, "pz"), 3.0F), "SetPosition and Translate must move the entity");
    Require(NearlyEqual(ArgumentOf(*done, "qx"), 1.5F) && NearlyEqual(ArgumentOf(*done, "qy"), 7.0F) && NearlyEqual(ArgumentOf(*done, "qz"), 3.0F), "A table without an axis must leave that axis alone");
    Require(NearlyEqual(rig.scene.Transforms().Get(entity).localPosition.x, 2.5F) && NearlyEqual(ArgumentOf(*done, "rx"), 2.5F), "The scene must hold what the script wrote");
    Require(ArgumentOf(*done, "same") == 1.0F, "Transform.GetPositionXYZ must read what Transform.GetPosition reads");
    Require(NearlyEqual(rig.scene.Transforms().Get(entity).worldPosition.y, 8.0F), "A written object without parent must have its world transform at once");
}

// `self` is one table for the life of an object: what a script writes into it stays there, separately for each object, a
// destroyed object leaves nothing behind for the next one, and the exposed variables an object holds are still current in it.
void RunSelfKeepsItsFieldsPerObjectTest() {
    SmallRig rig("function Tick(self, dt)\n  self.count = (self.count or 0) + 1\n  Emit('Count', { n = self.count, speed = self.variables.speed or -1 })\nend\n");
    const std::vector<kb::script::ScriptApiPin> pins{ kb::script::ScriptApiPin{ .name = "speed", .type = kb::script::ScriptValueType::Float } };
    const std::vector<kb::script::ScriptValue> defaults{ kb::script::ScriptValue{ 2.0F } };
    const std::vector<std::uint8_t> hasDefaults{ 1U };
    rig.host.LuaRuntime().SetScriptExposedVariables(kBenchAsset, pins, defaults, hasDefaults);
    const kb::scene::SceneEntity first = rig.Add();
    static_cast<void>(rig.Frame());
    static_cast<void>(rig.Frame());
    const kb::scene::SceneEntity second = rig.Add();
    rig.host.LuaRuntime().SetInstanceVariableOverride(second, kBenchAsset, "speed", kb::script::ScriptValue{ 9.5F });

    const auto countOf = [&](const kb::script::ScriptRuntimeExecutionResult& result, kb::scene::SceneEntity sender, std::string_view argument) {
        for (const kb::script::ScriptEvent& event : result.emittedEvents) {
            if (event.name == "Count" && event.sender.Id() == sender.Id()) return ArgumentOf(event, argument);
        }
        return -1000.0F;
    };
    const kb::script::ScriptRuntimeExecutionResult& third = rig.Frame();
    Require(countOf(third, first, "n") == 3.0F && countOf(third, second, "n") == 1.0F, "Each object must keep its own fields in self between calls");
    Require(countOf(third, first, "speed") == 2.0F && countOf(third, second, "speed") == 9.5F, "self.variables must hold the variables of its own object");
    rig.host.LuaRuntime().SetInstanceVariableOverride(second, kBenchAsset, "speed", kb::script::ScriptValue{ 4.0F });
    const kb::script::ScriptRuntimeExecutionResult& fourth = rig.Frame();
    Require(countOf(fourth, second, "n") == 2.0F && countOf(fourth, second, "speed") == 4.0F, "A variable changed between calls must be current in self.variables");

    rig.scene.Entities().Destroy(first);
    const kb::scene::SceneEntity replacement = rig.Add();
    const kb::script::ScriptRuntimeExecutionResult& fifth = rig.Frame();
    Require(countOf(fifth, replacement, "n") == 1.0F && countOf(fifth, second, "n") == 3.0F, "An object made after another was destroyed must start with an empty self");

    // A behaviour that starts over (as a reloaded one does) begins with an empty self again.
    static_cast<void>(rig.host.Runtime().ExecuteLifecycle(rig.scene, kb::script::ScriptLifecycleEvent::Created, 0.0F));
    const kb::script::ScriptRuntimeExecutionResult& sixth = rig.Frame();
    Require(countOf(sixth, second, "n") == 1.0F && countOf(sixth, replacement, "n") == 1.0F, "An object that starts over must not keep what its earlier start left in self");
}

// A script error reaches the console with the object's name, the line, a plain explanation, and the stack trace once.
void RunScriptErrorsReadInPlainWordsTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHostOptions options;
    options.installSceneSystem = true;
    kb::script::ScriptRuntimeHost host{ scene, options };
    Require(host.Succeeded(), "script error test host did not initialize");
    Require(host.LuaRuntime().LoadScript(kBenchAsset, "local scores = nil\nfunction Tick(self, dt)\n  local best = scores.top\nend\n", "Assets/Scripts/Door.lua").succeeded,
        "The failing script must load");
    const kb::scene::SceneObject door = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Door" });
    scene.Components().Behaviours().Set(door.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kBenchAsset.value, .backend = kb::scene::BehaviourBackend::Lua, .enabled = true });
    std::vector<std::string> lines;
    for (int frame = 0; frame < 3; ++frame) {
        static_cast<void>(scene.Runtime().Update(kFrameSeconds));
        for (std::string& line : host.DrainSceneSystemDiagnostics()) lines.push_back(std::move(line));
    }
    // The host also reports that the test asset is not registered with the asset manager; only the script error matters here.
    const auto doorLines = std::count_if(lines.begin(), lines.end(), [](const std::string& text) { return text.find("'Door'") != std::string::npos; });
    Require(doorLines == 1, "A script error that repeats every frame must be reported once");
    const std::string& line = *std::find_if(lines.begin(), lines.end(), [](const std::string& text) { return text.find("'Door'") != std::string::npos; });
    Require(line.find("'Door'") != std::string::npos, "The console line must name the object");
    Require(line.find("line 3: attempt to index a nil value") != std::string::npos, "The console line must name the line of the script");
    Require(line.find("has no value yet") != std::string::npos, "The console line must explain the usual cause in plain words");
    const std::size_t firstTrace = line.find("stack traceback:");
    Require(firstTrace != std::string::npos && line.find("stack traceback:", firstTrace + 1U) == std::string::npos, "The stack trace must be shown once");
    Require(line.find("Door.lua:3: in function", firstTrace) != std::string::npos, "The stack trace must show the line of the script that failed");
}

// A script that changes the scene in the middle of a phase: a behaviour it destroyed or disabled does not run after it, one
// it made does not run before the next frame, and the next frame runs the scene as it is then.
void RunPhaseSeesChangesMadeInsideItTest() {
    SmallRig rig("function Tick(self, dt) Emit('Ran') end\n");
    constexpr kb::assets::AssetId kChangerAsset{ 9702U };
    std::vector<kb::scene::SceneEntity> scripted;
    for (int index = 0; index < 5; ++index) scripted.push_back(rig.Add(kb::scene::BehaviourTickGroup::Gameplay, 10));
    const kb::scene::SceneObject changer = rig.scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Changer" });
    rig.scene.Components().Behaviours().Set(changer.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kChangerAsset.value, .backend = kb::scene::BehaviourBackend::Native, .enabled = true,
        .tickGroup = kb::scene::BehaviourTickGroup::Gameplay, .executionOrder = -10 });
    int frame = 0;
    kb::scene::SceneEntity born{};
    Require(rig.host.NativeBackend().RegisterLifecycle(kChangerAsset, kb::script::ScriptLifecycleEvent::Tick,
        [&](kb::script::ScriptExecutionContext& context) {
            if (++frame != 2) return;
            kb::scene::Scene& scene = context.GetScene();
            scene.Entities().Destroy(scripted[2]);
            kb::scene::BehaviourComponent disabled = *std::as_const(scene).Components().Behaviours().TryGet(scripted[3]);
            disabled.enabled = false;
            scene.Components().Behaviours().Set(scripted[3], disabled);
            born = rig.Add(kb::scene::BehaviourTickGroup::Gameplay, 10);
        }), "the changing callback must register");

    const auto ran = [&] {
        std::vector<std::uint64_t> senders = SendersOf(rig.Frame(), "Ran");
        std::sort(senders.begin(), senders.end());
        return senders;
    };
    const auto ids = [&](std::initializer_list<kb::scene::SceneEntity> entities) {
        std::vector<std::uint64_t> values;
        for (const kb::scene::SceneEntity entity : entities) values.push_back(entity.Id());
        std::sort(values.begin(), values.end());
        return values;
    };
    Require(ran() == ids({ scripted[0], scripted[1], scripted[2], scripted[3], scripted[4] }), "The first frame runs every behaviour");
    Require(ran() == ids({ scripted[0], scripted[1], scripted[4] }), "A behaviour destroyed or disabled by an earlier one in the phase must not run, and one it made must wait");
    Require(ran() == ids({ scripted[0], scripted[1], scripted[4], born }), "The next frame runs the behaviour that was made, and not the ones that went");
}

// The change stamp of the behaviours moves for every change to them and stays put for everything else, so a frame in
// which nothing about the behaviours changed has nothing to look at.
void RunBehaviourChangeStampTest() {
    SmallRig rig("function Tick(self, dt) end\n");
    const kb::scene::SceneEntity first = rig.Add();
    const kb::scene::SceneEntity second = rig.Add();
    static_cast<void>(rig.Frame());
    const auto stamp = [&] { return rig.scene.Components().Behaviours().ChangeStamp(); };
    const auto revision = [&] { return *rig.scene.Components().Behaviours().RevisionCounter(); };

    std::uint64_t before = stamp();
    std::uint64_t revisionBefore = revision();
    for (int index = 0; index < 3; ++index) static_cast<void>(rig.Frame());
    Require(stamp() == before && revision() == revisionBefore, "Frames that change no behaviour must leave the stamp alone");

    kb::scene::TransformComponent moved = rig.scene.Transforms().Get(first);
    moved.localPosition.x += 1.0F;
    rig.scene.Transforms().Set(first, moved);
    Require(stamp() == before && revision() == revisionBefore, "Moving an object that carries a behaviour must leave the stamp alone");
    const kb::scene::SceneObject plain = rig.scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Plain" });
    Require(stamp() == before && revision() == revisionBefore, "Making an object without a behaviour must leave the stamp alone");
    rig.scene.Entities().Destroy(plain.Entity());
    Require(stamp() == before && revision() == revisionBefore, "Destroying an object without a behaviour must leave the stamp alone");

    kb::scene::BehaviourComponent changed = *std::as_const(rig.scene).Components().Behaviours().TryGet(first);
    changed.executionOrder = 5;
    rig.scene.Components().Behaviours().Set(first, changed);
    Require(stamp() != before && revision() != revisionBefore, "Setting a behaviour must move the stamp");
    before = stamp();
    revisionBefore = revision();

    kb::scene::BehaviourComponent* writable = rig.scene.Components().Behaviours().TryGet(first);
    writable->enabled = false;
    rig.scene.Components().Behaviours().MarkModified(first);
    Require(stamp() != before && revision() != revisionBefore, "Marking a behaviour modified must move the stamp");
    before = stamp();
    revisionBefore = revision();

    rig.scene.Entities().Destroy(second);
    Require(stamp() != before && revision() != revisionBefore, "Destroying an entity that carries a behaviour must move the stamp");
    before = stamp();
    revisionBefore = revision();

    rig.scene.Components().Behaviours().Remove(first);
    Require(stamp() != before && revision() != revisionBefore, "Removing a behaviour must move the stamp");
}

// A registration made after the behaviours were running takes effect in the next frame, and a replaced one is the one
// that runs.
void RunNativeRegistrationChangesReachRunningBehavioursTest() {
    SmallRig rig("function Tick(self, dt) end\n");
    constexpr kb::assets::AssetId kNativeAsset{ 9703U };
    for (int index = 0; index < 3; ++index) {
        const kb::scene::SceneObject object = rig.scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Native" });
        rig.scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
            .behaviourAssetId = kNativeAsset.value, .backend = kb::scene::BehaviourBackend::Native, .enabled = true });
    }
    Require(rig.Frame().executedBehaviours == 0U, "Behaviours without a callback run nothing");
    int firstCalls = 0;
    int secondCalls = 0;
    Require(rig.host.NativeBackend().RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick,
        [&](kb::script::ScriptExecutionContext&) { ++firstCalls; }), "the first callback must register");
    Require(rig.Frame().executedBehaviours == 3U && firstCalls == 3, "A callback registered while the behaviours run must be called in the next frame");
    Require(rig.host.NativeBackend().RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick,
        [&](kb::script::ScriptExecutionContext&) { ++secondCalls; }), "the second callback must register");
    static_cast<void>(rig.Frame());
    Require(firstCalls == 3 && secondCalls == 3, "A replaced callback must no longer be called");
}

// An entry that fails does not stop the others in its phase, and the next frame runs them all again.
void RunFailingEntryDoesNotStopTheOthersTest() {
    SmallRig rig("function Tick(self, dt)\n  calls = (calls or 0) + 1\n  if calls % 2 == 0 then error('boom') end\n  Emit('Fine')\nend\n");
    for (int index = 0; index < 12; ++index) rig.Add();
    for (int frame = 0; frame < 3; ++frame) {
        const kb::script::ScriptRuntimeExecutionResult& result = rig.system.ExecuteFrame(rig.scene, kFrameSeconds);
        Require(result.diagnostics.size() == 6U && SendersOf(result, "Fine").size() == 6U && result.executedBehaviours == 6U,
            "Half of the entries fail, the other half must run in every frame");
        Require(result.diagnostics.front().message.find("boom") != std::string::npos, "A failure must report what the script raised");
    }
}

// A native class that only changes its own object: it may run on the workers.
struct AnyThreadMoverScript {
    static constexpr bool kRunOnAnyThread = true;
    int ticks = 0;
    void Tick(kb::script::ScriptExecutionContext* context) {
        ++ticks;
        if (kb::scene::TransformComponent* transform = context->SelfTransform(); transform != nullptr) {
            transform->localPosition.y = static_cast<float>(ticks);
        }
        if (context->Self().Id() % 97U == 0U) context->Emit("Moved");
    }
};

bool RegisterAnyThreadMover(kb::script::NativeScriptPluginApi* api) {
    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&
        kb::script::RegisterNativeScript<AnyThreadMoverScript>(api, "test.AnyThreadMover");
}

// Native calls that may run on any thread give the result of the serial loop, on the workers (many objects) and in one
// batch on the calling thread (few): every object moved once per frame, its world transform composed, the events it
// emitted delivered in the execution order.
void RunAnyThreadNativeScriptsTest() {
    for (const std::size_t count : { std::size_t{ 300U }, std::size_t{ 6000U } }) {
        kb::scene::Scene scene;
        kb::script::ScriptRuntimeHost host{ scene };
        kb::script::ScriptRuntimeSceneSystem system{ host.Runtime() };
        Require(host.Succeeded() && host.NativePlugins().LoadLinked("anythread", &RegisterAnyThreadMover).Succeeded(), "the any-thread test script did not register");
        Require(host.NativeBackend().BindAssetSymbol(kBenchAsset, "test.AnyThreadMover"), "the any-thread test symbol did not bind");
        std::vector<kb::scene::SceneObjectDesc> descs(count);
        std::vector<kb::scene::SceneEntity> entities;
        for (const kb::scene::SceneObject& object : scene.Entities().CreateObjects(descs)) {
            entities.push_back(object.Entity());
            scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{ .behaviourAssetId = kBenchAsset.value,
                .backend = kb::scene::BehaviourBackend::Native, .enabled = true });
        }
        std::vector<std::uint64_t> expected;
        for (const kb::scene::SceneEntity entity : entities) {
            if (entity.Id() % 97U == 0U) expected.push_back(entity.Id());
        }
        for (int frame = 0; frame < 3; ++frame) {
            const kb::script::ScriptRuntimeExecutionResult& result = system.ExecuteFrame(scene, kFrameSeconds);
            Require(result.Succeeded(), "an any-thread frame must not fail");
            Require(SendersOf(result, "Moved") == expected, "Events emitted on the workers must arrive in the execution order");
        }
        for (const kb::scene::SceneEntity entity : entities) {
            const kb::scene::TransformComponent transform = scene.Transforms().Get(entity);
            Require(transform.localPosition.y == 3.0F && transform.worldPosition.y == 3.0F, "Every any-thread object must move once a frame and be composed");
        }
    }
}

// A broadcast keeps its receivers between calls and still follows the scene: a new receiver is reached, a disabled or
// removed one is not.
void RunBroadcastReceiversFollowChangesTest() {
    SmallRig rig("function Ping(self, event) Emit('Pong') end\n");
    std::vector<kb::scene::SceneEntity> entities;
    for (int index = 0; index < 10; ++index) entities.push_back(rig.Add());
    static_cast<void>(rig.Frame());
    kb::script::ScriptEvent event;
    event.name = "Ping";
    const auto pongs = [&]() { return SendersOf(rig.host.Runtime().DispatchEvent(rig.scene, event, 0.0F), "Pong").size(); };
    Require(pongs() == 10U && pongs() == 10U, "Every receiver must be reached by every broadcast");
    entities.push_back(rig.Add());
    static_cast<void>(rig.Frame());
    Require(pongs() == 11U, "A behaviour added since the last broadcast must be reached");
    kb::scene::BehaviourComponent disabled = *std::as_const(rig.scene).Components().Behaviours().TryGet(entities[3]);
    disabled.enabled = false;
    rig.scene.Components().Behaviours().Set(entities[3], disabled);
    static_cast<void>(rig.Frame());
    Require(pongs() == 10U, "A disabled behaviour must not be reached");
    rig.scene.Components().Behaviours().Remove(entities[4]);
    static_cast<void>(rig.Frame());
    Require(pongs() == 9U, "A removed behaviour must not be reached");
}

// Every receiver of a broadcast reads the event as it was sent.
void RunBroadcastCarriesTheEventToEveryReceiverTest() {
    SmallRig rig("function Ping(self, event) Emit('Pong', { n = event.args.n, name = event.name }) end\n");
    for (int index = 0; index < 30; ++index) rig.Add();
    static_cast<void>(rig.Frame());
    kb::script::ScriptEvent event;
    event.name = "Ping";
    event.arguments.push_back(kb::script::ScriptEventArgument{ .name = "n", .value = kb::script::ScriptValue{ 7 } });
    const kb::script::ScriptRuntimeExecutionResult result = rig.host.Runtime().DispatchEvent(rig.scene, event, 0.0F);
    std::size_t pongs = 0U;
    for (const kb::script::ScriptEvent& emitted : result.emittedEvents) {
        if (emitted.name != "Pong") continue;
        ++pongs;
        Require(ArgumentOf(emitted, "n") == 7.0F, "A receiver of a broadcast must read the arguments of the event");
    }
    Require(pongs == 30U, "Every receiver of a broadcast must run its handler");
    const kb::script::ScriptRuntimeExecutionResult again = rig.host.Runtime().DispatchEvent(rig.scene, event, 0.0F);
    Require(again.executedBehaviours == 30U, "A second broadcast must reach every receiver again");
}

} // namespace

void RunScriptDispatchTests() {
    RunScriptErrorsReadInPlainWordsTest();
    RunDispatchOrderIsDeterministicTest();
    RunDispatchDoesNotAllocateInTheHotPathTest();
    RunCollectorStepStaysWithinBudgetTest();
    RunHotReloadKeepsFieldsTest();
    RunDispatchSkipsMissingCallbacksTest();
    RunTargetedEventReachesOneBehaviourTest();
    RunYieldingEntriesShareThreadsSafelyTest();
    RunTransformBindingsTest();
    RunSelfKeepsItsFieldsPerObjectTest();
    RunPhaseSeesChangesMadeInsideItTest();
    RunBehaviourChangeStampTest();
    RunNativeRegistrationChangesReachRunningBehavioursTest();
    RunFailingEntryDoesNotStopTheOthersTest();
    RunBroadcastCarriesTheEventToEveryReceiverTest();
    RunAnyThreadNativeScriptsTest();
    RunBroadcastReceiversFollowChangesTest();
}

// The numbers this runs are printed for comparison; the limits it requires are far looser than what the runtime does
// now and far tighter than what it did when every call rebuilt the script's whole API (several seconds a frame for
// 10 000 behaviours), so they hold on a slower or busy machine and still catch such a regression.
void RunScriptDispatchBenchmark() {
    for (const std::size_t count : { std::size_t{ 1000U }, std::size_t{ 10000U } }) {
        const int frames = count > 1000U ? 20 : 60;
        const double scale = static_cast<double>(count) / 10000.0;
        {
            Rig rig(count, kEmptyScript);
            const FrameStats stats = MeasureFrames(rig, frames);
            ReportFrames("empty Tick", count, stats);
            Require(stats.median < 100.0 * scale + 2.0, "A frame of empty Tick callbacks must stay within its budget");
            Require(stats.cppAllocations < 64.0, "A frame of empty Tick callbacks must not allocate engine memory per behaviour");
        }
        {
            Rig rig(count, kTypicalScript);
            const FrameStats stats = MeasureFrames(rig, frames);
            ReportFrames("typical Tick", count, stats);
            Require(stats.median < 150.0 * scale + 3.0, "A frame of typical Tick callbacks must stay within its budget");
            Require(stats.cppAllocations < 64.0, "A frame of typical Tick callbacks must not allocate engine memory per behaviour");
        }
        {
            Rig rig(count, kEmptyScript, true);
            const FrameStats stats = MeasureFrames(rig, frames);
            ReportFrames("empty Tick + @expose", count, stats);
            Require(stats.median < 100.0 * scale + 2.0, "Exposed variables must not make a frame slower once synchronized");
        }
    }

    {   // The first frame creates every instance (Created, Activated, Ready) and runs its first Tick.
        Rig rig(10000U, kTypicalScript, true);
        const std::size_t luaBefore = rig.host.LuaRuntime().LuaMemoryUsedBytes();
        BeginAllocationTally();
        const Clock::time_point start = Clock::now();
        rig.Frame();
        const double first = MillisecondsSince(start);
        const AllocationTally tally = EndAllocationTally();
        const std::size_t luaAfter = rig.host.LuaRuntime().LuaMemoryUsedBytes();
        std::printf("script-bench first frame           N=10000  %8.3f ms  c++ bytes allocated/instance %9.1f  lua bytes grown/instance %7.1f\n",
            first, static_cast<double>(tally.bytes) / 10000.0, (static_cast<double>(luaAfter) - static_cast<double>(luaBefore)) / 10000.0);

        Require(first < 400.0, "Creating 10 000 script instances and running their first Tick must stay within a few frames");

        const Clock::time_point reloadStart = Clock::now();
        Require(rig.host.LuaRuntime().ReloadScript(kBenchAsset, kTypicalScript, "Bench.lua").succeeded, "benchmark reload failed");
        const double reload = MillisecondsSince(reloadStart);
        const Clock::time_point afterReloadStart = Clock::now();
        rig.Frame();
        const double afterReload = MillisecondsSince(afterReloadStart);
        std::printf("script-bench hot reload            N=10000  reload call %8.3f ms  next frame %8.3f ms\n", reload, afterReload);
        Require(reload < 50.0 && afterReload < 150.0, "A hot reload and the frame after it must stay within a frame or two");
    }

    {   // One event for one entity among many: the cost is in finding the receiver.
        Rig rig(10000U, "function Tick(self, dt) end\nfunction Ping(self, event) end\n");
        rig.Frame();
        kb::script::ScriptEvent event;
        event.name = "Ping";
        event.target = rig.entities[5000];
        constexpr int kEvents = 40;
        const Clock::time_point start = Clock::now();
        std::size_t answered = 0U;
        for (int index = 0; index < kEvents; ++index) {
            answered += rig.host.Runtime().DispatchEvent(rig.scene, event, 0.0F).executedBehaviours;
        }
        const double perEvent = MillisecondsSince(start) / kEvents;
        std::printf("script-bench targeted event        N=10000  %8.3f ms per event  (answered %zu)\n", perEvent, answered);
        Require(answered == static_cast<std::size_t>(kEvents) && perEvent < 0.25, "An event for one entity must not cost a pass over every behaviour");
    }

    const struct { const char* label; const char* statement; } bindings[] = {
        { "local", "local _ = i" },
        { "Transform.GetPosition", "local p = Transform.GetPosition(self.entity)" },
        { "Transform.GetPositionXYZ", "local x, y, z = Transform.GetPositionXYZ(self.entity)" },
        { "Transform.SetPosition", "Transform.SetPosition(self.entity, 1, 2, 3)" },
        { "self:GetProperty", "local x = self:GetProperty('Transform', 'localPosition.x')" },
        { "self:SetProperty", "self:SetProperty('Transform', 'localPosition.x', 1.5)" },
        { "Input.IsPressed", "local b = Input.IsPressed('Jump')" },
    };
    for (const auto& binding : bindings) {
        bool succeeded = false;
        const double nanoseconds = BindingNanoseconds(binding.statement, succeeded);
        std::printf("script-bench binding %-24s %9.1f ns per call%s\n", binding.label, nanoseconds, succeeded ? "" : "  (script reported an error)");
        Require(succeeded, "A benchmark script reported an error");
        if (std::string_view{ binding.label } == "Transform.GetPosition") Require(nanoseconds < 700.0, "Transform.GetPosition must not go through the function registry");
        if (std::string_view{ binding.label } == "Transform.SetPosition") Require(nanoseconds < 600.0, "Transform.SetPosition must not go through the function registry");
        if (std::string_view{ binding.label } == "Input.IsPressed") Require(nanoseconds < 400.0, "Input.IsPressed must not go through the function registry");
    }
}

namespace {

[[nodiscard]] std::string EnvText(const char* name, const char* fallback) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0U;
    static_cast<void>(_dupenv_s(&value, &length, name));
    std::string text = value != nullptr ? std::string{ value } : std::string{};
    std::free(value);
    return text.empty() ? std::string{ fallback } : text;
#else
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::string{ value } : std::string{ fallback };
#endif
}

// The same three scripts exist as Lua and as native classes; a scenario runs one of them on every object of the scene.
constexpr const char* kFrameTypicalLua = R"(
local speed = 2.0
function Tick(self, dt)
  local x, y, z = Transform.GetPositionXYZ(self.entity)
  if Input.IsPressed("jump") then speed = -speed end
  local len = math.sqrt(x * x + y * y + z * z) + 1.0
  local d = (x / len) * 0.6 + (z / len) * 0.8
  Transform.SetPosition(self.entity, x + 0.6 * speed * dt, y + d * dt, z + 0.8 * speed * dt)
end
)";

// The typical script as written with the table form of Transform.GetPosition (scenario typical-table).
constexpr const char* kFrameTypicalTableLua = R"(
local speed = 2.0
function Tick(self, dt)
  local p = Transform.GetPosition(self.entity)
  if Input.IsPressed("jump") then speed = -speed end
  local len = math.sqrt(p.x * p.x + p.y * p.y + p.z * p.z) + 1.0
  local d = (p.x / len) * 0.6 + (p.z / len) * 0.8
  Transform.SetPosition(self.entity, p.x + 0.6 * speed * dt, p.y + d * dt, p.z + 0.8 * speed * dt)
end
)";

constexpr const char* kFrameHeavyLua = R"(
local function fib(n)
  if n < 2 then return n end
  return fib(n - 1) + fib(n - 2)
end
function Tick(self, dt)
  local phase = (self.acc or 0.0) * 1.0e-7
  local total = 0.0
  for i = 1, 100 do total = total + math.sin(i * 0.01 + phase) * math.sqrt(i) end
  local arr = {}
  for i = 1, 16 do arr[#arr + 1] = i * i end
  local tbl = {}
  for i = 1, 8 do tbl["k" .. i] = i end
  local sum = 0
  for _, v in pairs(tbl) do sum = sum + v end
  local text = ""
  for i = 1, 8 do text = text .. i .. "," end
  total = total + fib(10 + (math.floor(total) & 1)) + sum + #text + arr[16]
  self.acc = total
end
)";

constexpr const char* kFrameEventLua = R"(
function Ping(self, event)
  self.count = (self.count or 0) + 1
end
)";

const kb::input::InputActionName kFrameJumpAction{ "jump" };

// The typical script only changes its own object, so it may run on the workers; the inline variant runs it on the
// calling thread (scenario typical-inline) for comparison.
template <bool AnyThread>
struct FrameTypicalScript {
    static constexpr bool kRunOnAnyThread = AnyThread;
    float speed = 2.0F;
    void Tick(kb::script::ScriptExecutionContext* context) {
        kb::scene::TransformComponent* transform = context->SelfTransform();
        const float dt = context->DeltaSeconds();
        if (context->Input().IsActionPressed(kFrameJumpAction)) speed = -speed;
        const float x = transform->localPosition.x;
        const float y = transform->localPosition.y;
        const float z = transform->localPosition.z;
        const float length = std::sqrt(x * x + y * y + z * z) + 1.0F;
        const float d = (x / length) * 0.6F + (z / length) * 0.8F;
        transform->localPosition.x = x + 0.6F * speed * dt;
        transform->localPosition.y = y + d * dt;
        transform->localPosition.z = z + 0.8F * speed * dt;
    }
};

[[nodiscard]] int FrameFib(int n) { return n < 2 ? n : FrameFib(n - 1) + FrameFib(n - 2); }

struct FrameHeavyScript {
    double acc = 0.0;
    void Tick(kb::script::ScriptExecutionContext*) {
        const double phase = acc * 1.0e-7;
        double total = 0.0;
        for (int i = 1; i <= 100; ++i) total += std::sin(i * 0.01 + phase) * std::sqrt(static_cast<double>(i));
        std::vector<int> arr;
        for (int i = 1; i <= 16; ++i) arr.push_back(i * i);
        std::unordered_map<std::string, int> table;
        for (int i = 1; i <= 8; ++i) table["k" + std::to_string(i)] = i;
        int sum = 0;
        for (const auto& entry : table) sum += entry.second;
        std::string text;
        for (int i = 1; i <= 8; ++i) text += std::to_string(i) + ",";
        total += FrameFib(10 + (static_cast<int>(std::floor(total)) & 1)) + sum + static_cast<double>(text.size()) + arr[15];
        acc = total;
    }
};

std::uint64_t frameEventCount = 0U;
void FramePing(kb::script::ScriptExecutionContext*, const kb::script::ScriptEvent*) { ++frameEventCount; }

struct FrameEmptyScript {
    float speed = 2.0F;
    void Tick(kb::script::ScriptExecutionContext*) {}
};

bool RegisterFrameScripts(kb::script::NativeScriptPluginApi* api) {
    return api != nullptr && api->version == kb::script::kNativeScriptPluginApiVersion &&
        kb::script::RegisterNativeScript<FrameEmptyScript>(api, "bench.Empty") &&
        kb::script::RegisterNativeScript<FrameTypicalScript<true>>(api, "bench.Typical") &&
        kb::script::RegisterNativeScript<FrameTypicalScript<false>>(api, "bench.TypicalInline") &&
        kb::script::RegisterNativeScript<FrameHeavyScript>(api, "bench.Heavy") &&
        api->registerEvent(api->user, "bench.Event", "Ping", &FramePing);
}

} // namespace

// One scenario of the scripting comparison: `count` objects, each running the chosen script every frame (or one event
// broadcast to all of them), measured as wall time per frame. Settings come from the environment so that every run is
// a fresh process with its own peak memory: KB_BENCH_BACKEND (lua, native), KB_BENCH_SCENARIO (empty, typical, heavy,
// event; native also typical-inline, Lua also typical-table and custom, whose script text is KB_BENCH_SOURCE), KB_BENCH_COUNT, KB_BENCH_FRAMES, KB_BENCH_WARMUP, and for Lua
// KB_BENCH_LUA_INSTRUCTIONS (the per-call instruction budget; 0 turns it off, unset keeps the default).
void RunScriptFrameBenchmark() {
    const std::string backend = EnvText("KB_BENCH_BACKEND", "lua");
    const std::string scenario = EnvText("KB_BENCH_SCENARIO", "typical");
    const std::size_t count = static_cast<std::size_t>(std::atoi(EnvText("KB_BENCH_COUNT", "1000").c_str()));
    const int frames = std::atoi(EnvText("KB_BENCH_FRAMES", "600").c_str());
    const int warmup = std::atoi(EnvText("KB_BENCH_WARMUP", "60").c_str());
    const bool native = backend == "native";
    Require(native || backend == "lua", "KB_BENCH_BACKEND must be lua or native");

    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::script::ScriptRuntimeSceneSystem system{ host.Runtime() };
    Require(host.Succeeded(), "script frame benchmark host did not initialize");
    if (native) {
        Require(host.NativePlugins().LoadLinked("bench", &RegisterFrameScripts).Succeeded(), "native benchmark scripts did not register");
        const char* symbol = scenario == "empty" ? "bench.Empty" : scenario == "typical" ? "bench.Typical" : scenario == "typical-inline" ? "bench.TypicalInline"
            : scenario == "heavy" ? "bench.Heavy" : "bench.Event";
        Require(host.NativeBackend().BindAssetSymbol(kBenchAsset, symbol), "native benchmark symbol did not bind");
    } else {
        if (const std::string budget = EnvText("KB_BENCH_LUA_INSTRUCTIONS", ""); !budget.empty()) {
            kb::script::ScriptExecutionBudgetSettings settings;
            settings.luaInstructionsPerBehaviour = static_cast<std::size_t>(std::strtoull(budget.c_str(), nullptr, 10));
            host.LuaRuntime().SetExecutionBudgetSettings(settings);
        }
        const std::string customSource = scenario == "custom" ? EnvText("KB_BENCH_SOURCE", kEmptyScript) : std::string{};
        const char* source = scenario == "custom" ? customSource.c_str() : scenario == "empty" ? kEmptyScript : scenario == "typical" ? kFrameTypicalLua
            : scenario == "typical-table" ? kFrameTypicalTableLua : scenario == "heavy" ? kFrameHeavyLua : kFrameEventLua;
        Require(host.LuaRuntime().LoadScript(kBenchAsset, source, "Bench.lua").succeeded, "script frame benchmark script did not load");
    }
    std::vector<kb::scene::SceneObjectDesc> descs(count);
    for (std::size_t index = 0U; index < count; ++index) {
        descs[index].transform.localPosition = kb::scene::Vec3{ static_cast<float>(index % 100U), 0.0F, static_cast<float>(index / 100U) };
    }
    for (const kb::scene::SceneObject& object : scene.Entities().CreateObjects(descs)) {
        scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{ .behaviourAssetId = kBenchAsset.value,
            .backend = native ? kb::scene::BehaviourBackend::Native : kb::scene::BehaviourBackend::Lua, .enabled = true });
    }
    const bool event = scenario == "event";
    kb::script::ScriptEvent ping;
    ping.name = "Ping";
    const auto runFrame = [&]() {
        if (event) {
            const kb::script::ScriptRuntimeExecutionResult& result = host.Runtime().DispatchEvent(scene, ping, 0.0F);
            Require(result.executedBehaviours == count, "the event must reach every object");
        } else {
            static_cast<void>(system.ExecuteFrame(scene, kFrameSeconds));
            if (!native) host.LuaRuntime().StepGarbageCollector();
            Require(system.LastResult().Succeeded(), system.LastResult().diagnostics.empty() ? "script frame benchmark frame failed" : system.LastResult().diagnostics.front().message.c_str());
        }
    };
    // The first frame creates the instances; it is reported separately and is not part of the percentiles.
    const Clock::time_point first = Clock::now();
    if (event) static_cast<void>(system.ExecuteFrame(scene, kFrameSeconds)); else runFrame();
    const double firstMilliseconds = MillisecondsSince(first);
    for (int frame = 0; frame < warmup; ++frame) runFrame();
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(frames));
    for (int frame = 0; frame < frames; ++frame) {
        const Clock::time_point start = Clock::now();
        runFrame();
        times.push_back(MillisecondsSince(start));
    }
    std::sort(times.begin(), times.end());
    const auto at = [&](double q) { return times[std::min(times.size() - 1U, static_cast<std::size_t>(static_cast<double>(times.size()) * q))]; };
    std::printf("RESULT backend=%s scenario=%s count=%zu frames=%d p50=%.4f p95=%.4f p99=%.4f max=%.4f first=%.3f\n",
        backend.c_str(), scenario.c_str(), count, frames, at(0.5), at(0.95), at(0.99), times.back(), firstMilliseconds);
}

} // namespace kb::tests
