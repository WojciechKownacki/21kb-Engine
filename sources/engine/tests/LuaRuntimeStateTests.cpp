#include "TestSupport.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptExecutionContext.hpp"
#include "engine/modules/EngineModuleExports.hpp"

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
    kb::tests::Require(!runtime.RestoreRuntimeState(*snapshot, error) && !error.empty(), "Reloaded Lua code accepted old snapshot");
    std::cout << "Lua graph payload_bytes=" << snapshot->PayloadBytes() << " restore_us=" << restoreUs << " replay_frames=60\n";
}

void RunUnsupportedStateAndLifetime() {
    std::string error;
    kb::script::PucLuaScriptRuntime runtime;
    kb::tests::Require(runtime.LoadScript(kAsset, "pending = coroutine.create(function() end)", "Thread.lua").succeeded,
        "Lua thread fixture failed to load");
    kb::tests::Require(runtime.CaptureRuntimeState(error) == nullptr && !error.empty(), "Lua silently skipped coroutine thread state");
    std::unique_ptr<kb::core::RuntimeStateSnapshot> detached;
    {
        kb::script::PucLuaScriptRuntime source;
        kb::tests::Require(source.LoadScript(kAsset, "value = {answer=42}", "Detached.lua").succeeded, "Detached Lua fixture failed to load");
        detached = source.CaptureRuntimeState(error);
        kb::tests::Require(detached != nullptr, "Detached Lua capture failed");
    }
    kb::tests::Require(!runtime.ValidateRuntimeState(*detached, error), "Destroyed Lua VM snapshot accepted");
    detached.reset();
    std::cout << "Lua unsupported state rejection and detached lifetime passed\n";
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
}

int main() {
    RunReplay();
    RunUnsupportedStateAndLifetime();
    RunOldModuleRejection();
}
