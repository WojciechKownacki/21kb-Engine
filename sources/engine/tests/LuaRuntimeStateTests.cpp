#include "TestSupport.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/modules/EngineModuleExports.hpp"
#include "engine/script/ScriptSharedState.hpp"

#include <chrono>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <filesystem>

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
}

int main() {
    RunReplay();
    RunThreadStateAndLifetime();
    RunSuspendedCoroutineReplay();
    RunHistoryBudget();
    RunOldModuleRejection();
}
