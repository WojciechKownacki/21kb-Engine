#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptEvent.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
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
  Emit('Done', { ok = ok1 and ok2 and ok3 and ok4 and not bad, px = p.x, py = p.y, pz = p.z, qx = q.x, qy = q.y, qz = q.z,
                 rx = r.x, missing = missing == nil, pressed = Input.IsPressed('NoSuchAction') })
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

} // namespace kb::tests
