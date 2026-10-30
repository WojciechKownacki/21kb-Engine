#include "TestSupport.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/modules/EngineModuleExports.hpp"
#include "engine/script/ScriptSharedState.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneRuntime.hpp"

#include <chrono>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <array>

namespace {
constexpr kb::assets::AssetId kAsset{7201U};

void RequireSame(const std::vector<kb::script::ScriptEventArgument>& expected,
    const std::vector<kb::script::ScriptEventArgument>& actual) {
    kb::tests::Require(expected.size() == actual.size(), "Lua replay changed output fields");
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (expected[i].name != actual[i].name || expected[i].value != actual[i].value) {
            std::cerr << "Lua field mismatch: " << actual[i].name << '\n';
            kb::tests::Require(false, "Lua table/closure/RNG replay differs");
        }
    }
}

void RunReplay() {
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    kb::scene::Scene scene{std::move(descriptor)};
    const auto entity = scene.Entities().CreateEntity();
    std::vector<kb::script::ScriptEvent> events;
    kb::script::ScriptExecutionContext context{scene, entity, kAsset, kb::scene::BehaviourBackend::Lua,
        kb::script::ScriptLifecycleEvent::Tick, 1.0F/60.0F, &events};
    kb::scene::BehaviourComponent behaviour{.behaviourAssetId = kAsset.value, .backend = kb::scene::BehaviourBackend::Lua};
    kb::script::PucLuaScriptRuntime runtime;
    const auto loaded = runtime.LoadScript(kAsset, R"(
local counter = 0
local state = { value = 0 }
state.cycle = state
local alias = state
local function advance()
    counter = counter + 1
    state.value = state.value + 3
    return counter + state.value
end
math.randomseed(12345)
function Tick(self, dt)
    total = (total or 0) + 1
    assert(alias == state and state.cycle == state)
    Emit("State", { counter=counter, total=total, value=state.value,
        closure=advance(), random=math.random(1,1000000) })
end
)", "StateReplay.lua");
    kb::tests::Require(loaded.succeeded, "Lua state fixture failed to load");
    auto tick = [&] {
        events.clear();
        const auto result = runtime.ExecuteLifecycle(behaviour, context);
        if (!result.Succeeded()) for (const auto& diagnostic : result.diagnostics) std::cerr << diagnostic.message << '\n';
        kb::tests::Require(result.executed && result.Succeeded() && events.size() == 1U, "Lua state fixture tick failed");
        auto output = events.front().arguments;
        std::sort(output.begin(), output.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        return output;
    };
    for (int i = 0; i < 5; ++i) static_cast<void>(tick());
    std::string error;
    auto snapshot = runtime.CaptureRuntimeState(error);
    if (!snapshot) std::cerr << "Capture: " << error << '\n';
    kb::tests::Require(snapshot != nullptr && error.empty() && snapshot->PayloadBytes() != 0U, "Lua capture failed");
    std::vector<std::vector<kb::script::ScriptEventArgument>> expected;
    for (int i = 0; i < 60; ++i) expected.push_back(tick());
    const auto started = std::chrono::steady_clock::now();
    const bool restored = runtime.RestoreRuntimeState(*snapshot, error);
    if (!restored) std::cerr << "Restore: " << error << '\n';
    kb::tests::Require(restored && error.empty(), "Lua restore failed");
    const auto restoreUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now()-started).count();
    for (const auto& output : expected) RequireSame(output, tick());
    kb::tests::Require(runtime.RestoreRuntimeState(*snapshot, error), "Repeated Lua restore failed");
    RequireSame(expected.front(), tick());
    kb::script::PucLuaScriptRuntime other;
    kb::tests::Require(!other.RestoreRuntimeState(*snapshot, error) && !error.empty(), "Foreign Lua VM accepted snapshot");
    kb::tests::Require(runtime.ReloadScript(kAsset, "function Tick() end", "Changed.lua").succeeded, "Lua fixture reload failed");
    kb::tests::Require(runtime.RestoreRuntimeState(*snapshot, error), "Lua code/catalog state did not restore");
    RequireSame(expected.front(), tick());
    std::cout << "Lua heap payload_bytes=" << snapshot->PayloadBytes() << " restore_us=" << restoreUs << " replay_frames=60\n";
}

void RunThreadStateAndLifetime() {
    std::string error;
    kb::script::PucLuaScriptRuntime runtime;
    kb::tests::Require(runtime.LoadScript(kAsset, "pending = coroutine.create(function() end)", "Thread.lua").succeeded,
        "Lua thread fixture failed to load");
    auto threadState = runtime.CaptureRuntimeState(error);
    kb::tests::Require(threadState != nullptr && runtime.RestoreRuntimeState(*threadState, error), "Lua thread heap state did not restore");
    std::unique_ptr<kb::core::RuntimeStateSnapshot> detached;
    {
        kb::script::PucLuaScriptRuntime source;
        kb::tests::Require(source.LoadScript(kAsset, "value = {answer=42}", "Detached.lua").succeeded, "Detached Lua fixture failed to load");
        detached = source.CaptureRuntimeState(error);
        kb::tests::Require(detached != nullptr, "Detached Lua capture failed");
    }
    kb::tests::Require(!runtime.ValidateRuntimeState(*detached, error), "Destroyed Lua VM snapshot accepted");
    detached.reset();
    std::cout << "Lua thread state and detached lifetime passed\n";
}

void RunSuspendedCoroutineReplay() {
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    kb::scene::Scene scene{std::move(descriptor)};
    const auto entity = scene.Entities().CreateEntity();
    kb::script::PucLuaScriptRuntime runtime;
    kb::tests::Require(runtime.LoadScript(kAsset, R"(
local count = 0
local iterator = string.gmatch("abcdefghijklmnop", ".")
math.randomseed(717)
function Tick(self, dt)
    local read = GetShared
    local emit = Emit
    while count < 12 do
        count = count + 1
        local garbage = {}
        for i=1,700 do garbage[i] = string.rep(tostring(i), 20) end
        emit("Coroutine", {count=count, marker=read("marker"), random=math.random(1000000), char=iterator()})
        coroutine.yield()
    end
end
)", "CoroutineState.lua").succeeded, "Coroutine replay fixture load failed");
    kb::scene::BehaviourComponent behaviour{.behaviourAssetId=kAsset.value, .backend=kb::scene::BehaviourBackend::Lua};
    struct Frame {
        kb::script::ScriptSharedState shared;
        std::vector<kb::script::ScriptEvent> events;
        std::unique_ptr<kb::script::ScriptExecutionContext> context;
    };
    std::vector<std::unique_ptr<Frame>> frames;
    auto tick = [&](int marker) {
        auto frame = std::make_unique<Frame>();
        kb::tests::Require(frame->shared.Set("marker", kb::script::ScriptValue{marker}), "Frame marker write failed");
        frame->context = std::make_unique<kb::script::ScriptExecutionContext>(scene, entity, kAsset,
            kb::scene::BehaviourBackend::Lua, kb::script::ScriptLifecycleEvent::Tick, 1.0F/60.0F,
            &frame->events, nullptr, &frame->shared);
        const auto result = runtime.ExecuteLifecycle(behaviour, *frame->context);
        if (!result.Succeeded()) for (const auto& diagnostic : result.diagnostics) std::cerr << diagnostic.message << '\n';
        kb::tests::Require(result.executed && result.Succeeded() && frame->events.size()==1U,
            "Suspended coroutine used an expired frame context or failed to resume");
        auto output = frame->events.front().arguments;
        std::sort(output.begin(), output.end(), [](const auto& a,const auto& b){return a.name<b.name;});
        for (const auto& value : output) if (value.name=="marker") kb::tests::Require(value.value.AsInt()==marker,
            "Retained Lua API closure used the earlier frame context");
        frames.push_back(std::move(frame));
        return output;
    };
    static_cast<void>(tick(100));
    kb::tests::Require(runtime.SuspendedCoroutineCount()==1U, "Coroutine was not suspended at capture");
    std::string error;
    const auto baseline = runtime.LuaMemoryOwnedBytes();
    auto first = runtime.CaptureRuntimeState(error);
    if (!first) std::cerr << error << '\n';
    kb::tests::Require(first != nullptr, "Suspended coroutine capture failed");
    const auto oneImage = runtime.LuaMemoryOwnedBytes();
    kb::tests::Require(oneImage > baseline, "Heap image memory was not accounted");
    std::vector<std::vector<kb::script::ScriptEventArgument>> expected;
    for (int i=101; i<106; ++i) expected.push_back(tick(i));
    auto later = runtime.CaptureRuntimeState(error);
    kb::tests::Require(later != nullptr, "Branched coroutine image capture failed");
    const auto started=std::chrono::steady_clock::now();
    kb::tests::Require(runtime.RestoreRuntimeState(*first,error), "Suspended coroutine restore failed");
    const auto restoreUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();
    for (std::size_t i=0;i<expected.size();++i) RequireSame(expected[i],tick(static_cast<int>(i)+101));
    kb::tests::Require(runtime.RestoreRuntimeState(*later,error), "Later coroutine branch restore failed");
    const auto next=tick(106);
    kb::tests::Require(runtime.RestoreRuntimeState(*later,error), "Repeated later branch restore failed");
    RequireSame(next,tick(106));
    const auto beforeRelease=runtime.LuaMemoryOwnedBytes();
    first.reset();later.reset();
    kb::tests::Require(runtime.LuaMemoryOwnedBytes()<beforeRelease, "Released Lua history retained its owned storage");
    std::cout << "Lua coroutine heap replay passed; restore_us=" << restoreUs << " owned_bytes=" << runtime.LuaMemoryOwnedBytes() << '\n';
}

void RunOldModuleRejection() {
    const auto marker = std::filesystem::temp_directory_path() / ("kb-state-abi-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".marker");
#if defined(_WIN32)
    _putenv_s("KB_OLD_ENGINE_MODULE_ABI_CREATE_MARKER", marker.string().c_str());
#else
    setenv("KB_OLD_ENGINE_MODULE_ABI_CREATE_MARKER", marker.string().c_str(), 1);
#endif
    kb::project::ProjectDescriptor descriptor;
    descriptor.disableEnginePluginsByDefault = true;
    descriptor.plugins.push_back({.name = "Tests.OldEngineModuleAbi", .binaryPath = KB_OLD_STATE_ABI_PLUGIN_PATH, .enabled = true});
    kb::scene::Scene scene{std::move(descriptor)};
#if defined(_WIN32)
    _putenv_s("KB_OLD_ENGINE_MODULE_ABI_CREATE_MARKER", "");
#else
    unsetenv("KB_OLD_ENGINE_MODULE_ABI_CREATE_MARKER");
#endif
    bool versionDiagnostic = false;
    for (const auto& diagnostic : scene.ModuleDiagnostics()) {
        versionDiagnostic = versionDiagnostic || (diagnostic.find("unsupported engine module ABI version") != std::string::npos
            && diagnostic.find("engine ABI " + std::to_string(kb::modules::kEngineModuleAbiVersion)) != std::string::npos);
    }
    kb::tests::Require(versionDiagnostic && !scene.IsModuleActive("Tests.OldEngineModuleAbi") && !std::filesystem::exists(marker),
        "Old C++ module was not rejected before its factory after the Lua layout change");
    std::cout << "Older module rejected before factory; engine ABI=" << kb::modules::kEngineModuleAbiVersion << '\n';
}

void RunHistoryBudget() {
    kb::script::PucLuaScriptRuntime runtime;
    std::string error;
    kb::tests::Require(runtime.LoadScript(kAsset, "value = string.rep('a', 4096)", "Budget.lua").succeeded,
        "History budget fixture failed");
    kb::script::ScriptExecutionBudgetSettings settings;
    settings.luaMemoryBytes = runtime.LuaMemoryOwnedBytes() + 1U;
    runtime.SetExecutionBudgetSettings(settings);
    const auto before = runtime.LuaMemoryOwnedBytes();
    kb::tests::Require(runtime.CaptureRuntimeState(error) == nullptr && !error.empty(),
        "History capture ignored its memory budget");
    kb::tests::Require(runtime.LuaMemoryOwnedBytes() == before, "Refused history capture retained storage");
    kb::tests::Require(!runtime.LoadScript({kAsset.value + 1U}, "value = string.rep('b', 1000000)", "OverBudget.lua").succeeded,
        "Lua allocator ignored the execution memory budget");
    settings.luaMemoryBytes = 0U;
    runtime.SetExecutionBudgetSettings(settings);
    auto image = runtime.CaptureRuntimeState(error);
    kb::tests::Require(image != nullptr && runtime.RestoreRuntimeState(*image,error),
        "Lua VM did not recover after allocation refusal");
    std::cout << "Lua history admission and execution memory budget passed\n";
}

void RunHostContinuation() {
    const auto root = std::filesystem::temp_directory_path() / ("kb-host-state-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "Assets");
    {
        std::ofstream source{root / "Assets/State.lua"};
        source << R"(
local count = 0
local read, write
local callback = function(event)
    write("total", (read("total") or 0) + event.args.amount)
end
function Created(self)
    read, write = GetShared, SetShared
    write("created", (read("created") or 0) + 1)
    Events.Subscribe("Pulse", callback)
end
function Tick(self, dt)
    while true do
        count = count + 1
        write("steps", count)
        write("random", math.random(1000000))
        Events.EmitDeferred("Pulse", {amount=count})
        coroutine.yield()
    end
end
)";
    }
    std::unique_ptr<kb::core::RuntimeStateSnapshot> detached;
    {
        kb::project::ProjectDescriptor descriptor;
        descriptor.disableEnginePluginsByDefault = true;
        kb::scene::Scene scene{std::move(descriptor)};
        kb::tests::Require(scene.Assets().MountProject(root) && scene.Assets().Discover()==1U, "Host state fixture mount failed");
        const auto* metadata=scene.Assets().Manager().Registry().FindByPath("/Game/State.lua");
        kb::tests::Require(metadata!=nullptr, "Host state script metadata missing");
        kb::script::ScriptRuntimeHost host{scene};
        kb::tests::Require(host.Succeeded() && host.AssetPreparer().PrepareAsset(metadata->id).Succeeded()
            && host.InstallSceneSystem(), "Host state fixture initialization failed");
        std::string error;
        auto empty=host.CaptureRuntimeState(error);
        kb::tests::Require(empty!=nullptr, "Empty host capture failed");
        const auto entity=scene.Entities().CreateEntity();
        scene.Components().Behaviours().Set(entity, {.behaviourAssetId=metadata->id.value,
            .backend=kb::scene::BehaviourBackend::Lua, .enabled=true});
        auto observation=host.Runtime().Events().Observe("Pulse");
        auto frame=[&] {
            kb::tests::Require(scene.Runtime().Update(0.01F), "Host state frame failed");
            const auto* result=host.InstalledSceneSystemLastResult();
            const auto* prepared=host.InstalledSceneSystemLastPrepareResult();
            if (result && !result->Succeeded()) for(const auto& diagnostic:result->diagnostics) std::cerr<<diagnostic.message<<'\n';
            kb::tests::Require(result && result->Succeeded() && prepared && prepared->Succeeded(), "Host continuation emitted diagnostics");
            auto value=[&](const char* name) {const auto v=host.SharedState().Get(name);return v?v->AsInt():0;};
            return std::array<std::int64_t,5>{value("created"),value("steps"),value("total"),value("random"),
                static_cast<std::int64_t>(observation->Sequence())};
        };
        static_cast<void>(frame());
        auto future=host.CaptureRuntimeState(error);
        kb::tests::Require(future!=nullptr, "Subscribed host capture failed");
        kb::tests::Require(host.LuaRuntime().CaptureRuntimeState(error)==nullptr, "Standalone VM silently captured host events");
        kb::tests::Require(host.RestoreRuntimeState(*empty,error), "Host empty branch restore failed");
        observation=host.Runtime().Events().Observe("Pulse");
        static_cast<void>(frame());
        future.reset();
        const auto afterRelease=frame();
        kb::tests::Require(afterRelease[0]==1 && afterRelease[1]==2 && afterRelease[2]==1,
            "Releasing a future callback removed the current registry reference or lifecycle repeated");
        auto baseline=host.CaptureRuntimeState(error);
        kb::tests::Require(baseline!=nullptr && host.LuaRuntime().SuspendedCoroutineCount()==1U, "Host suspended capture failed");
        std::vector<std::array<std::int64_t,5>> expected;
        for(int i=0;i<60;++i) expected.push_back(frame());
        kb::tests::Require(host.RestoreRuntimeState(*baseline,error), "Host subscribed branch restore failed");
        for(const auto& values:expected) kb::tests::Require(values==frame(), "Host event/shared/coroutine replay differs");
        const auto native=host.Runtime().Events().Subscribe("External",[](const kb::script::ScriptEvent&){});
        kb::tests::Require(host.CaptureRuntimeState(error)==nullptr && !error.empty(), "Host silently skipped external callback state");
        static_cast<void>(host.Runtime().Events().Unsubscribe(native));
        detached=host.CaptureRuntimeState(error);
        kb::tests::Require(detached!=nullptr, "Host detached image capture failed");
        static_cast<void>(observation);
    }
    detached.reset();
    std::filesystem::remove_all(root);
    std::cout<<"Lua host lifecycle/shared/deferred subscription/coroutine60-frame replay and detached history passed\n";
}

void RunHostFacadeDestruction() {
    const auto root=std::filesystem::temp_directory_path()/("kb-host-facade-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"Assets");
    {
        std::ofstream file{root/"Assets/State.lua"};
        file<<R"(
local ticks=0
function Tick(self, dt)
    ticks=ticks+1
    if ticks==1 then CallFunction("ReleaseHost", {}) end
    SetShared("ticks",ticks)
end
)";
    }
    std::unique_ptr<kb::core::RuntimeStateSnapshot> detached;
    {
        kb::project::ProjectDescriptor descriptor;
        descriptor.disableEnginePluginsByDefault=true;
        kb::scene::Scene scene{std::move(descriptor)};
        kb::tests::Require(scene.Assets().MountProject(root) && scene.Assets().Discover()==1U,"Host facade fixture mount failed");
        const auto* script=scene.Assets().Manager().Registry().FindByPath("/Game/State.lua");
        kb::tests::Require(script!=nullptr,"Host facade fixture metadata missing");
        const auto entity=scene.Entities().CreateEntity();
        scene.Components().Behaviours().Set(entity,{.behaviourAssetId=script->id.value,.backend=kb::scene::BehaviourBackend::Lua});
        auto host=std::make_unique<kb::script::ScriptRuntimeHost>(scene);
        auto* runtime=&host->Runtime();
        kb::tests::Require(host->RegisterFunction({.signature={.name="ReleaseHost",.description="Release the script host facade during dispatch"},
            .callback=[&host](const kb::script::ScriptFunctionCallContext&,std::span<const kb::script::ScriptFunctionArgument>) {
                host.reset();return kb::script::ScriptFunctionCallResult{.executed=true};
            }}),"Host facade release callback registration failed");
        kb::tests::Require(host->InstallSceneSystem(),"Host facade system install failed");
        const auto systemCount=scene.Runtime().SceneSystemCount();
        kb::tests::Require(scene.Runtime().Update(0.01F) && !host && scene.Runtime().SceneSystemCount()==systemCount,
            "Host facade was not destroyed during active dispatch");
        kb::tests::Require(runtime->SharedState().Get("ticks")->AsInt()==1,"Runtime state did not outlive the facade");
        std::string error;
        auto image=scene.Runtime().CaptureRuntimeState(error);
        kb::tests::Require(image!=nullptr,"Scene provider retained an expired host facade");
        kb::tests::Require(scene.Runtime().Update(0.01F) && runtime->SharedState().Get("ticks")->AsInt()==2,
            "Facade-free script state did not continue");
        kb::tests::Require(scene.Runtime().RestoreRuntimeState(*image,error) && scene.Runtime().Update(0.01F)
            && runtime->SharedState().Get("ticks")->AsInt()==2,"Facade-free scene checkpoint did not replay");
        detached=scene.Runtime().CaptureRuntimeState(error);
        kb::tests::Require(detached!=nullptr,"Facade-free detached capture failed");
    }
    detached.reset();
    std::filesystem::remove_all(root);
    std::cout<<"Host facade destruction during dispatch and shared state checkpoint passed\n";
}
}

int main() {
    RunReplay();
    RunThreadStateAndLifetime();
    RunSuspendedCoroutineReplay();
    RunHistoryBudget();
    RunHostContinuation();
    RunHostFacadeDestruction();
    RunOldModuleRejection();
}
