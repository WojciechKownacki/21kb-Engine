#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/LuaScriptBackend.hpp"
#include "engine/script/NativeScriptBuildPipeline.hpp"
#include "engine/script/NativeScriptBackend.hpp"
#include "engine/script/NativeScriptPluginManager.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/script/ScriptBehaviourBindingService.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeAssetPreparer.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptSceneVisualGraphBindings.hpp"
#include "engine/script/ScriptSharedVisualGraphBindings.hpp"
#include "engine/script/VisualGraphScriptBackend.hpp"
#include "engine/visual/VisualGraphCompiler.hpp"
#include "engine/visual/VisualGraphNativeCodeGenerator.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <array>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

#ifndef KB_NATIVE_SCRIPT_TEST_PLUGIN_PATH
#define KB_NATIVE_SCRIPT_TEST_PLUGIN_PATH ""
#endif

class FakeLuaRuntime final : public kb::script::ILuaScriptRuntime {
public:
    bool emitLifecycleEvent = true;
    kb::scene::SceneEntity lifecycleSelf{};
    kb::scene::SceneEntity eventSelf{};
    float lifecycleDelta = 0.0F;
    std::string receivedEventName;
    std::size_t receivedArgumentCount = 0;
    std::size_t eventExecutionCount = 0;

    kb::script::ScriptBackendExecutionResult ExecuteLifecycle(
        const kb::scene::BehaviourComponent& behaviour,
        kb::script::ScriptExecutionContext& context) override {
        lifecycleSelf = context.Self();
        lifecycleDelta = context.DeltaSeconds();
        if (emitLifecycleEvent) {
            context.Emit("LuaReady", {
                                         kb::script::ScriptEventArgument{
                                             .name = "asset",
                                             .value = kb::script::ScriptValue{static_cast<int>(behaviour.behaviourAssetId)},
                                         },
                                     });
        }
        return kb::script::ScriptBackendExecutionResult{.executed = true};
    }

    kb::script::ScriptBackendExecutionResult ExecuteEvent(
        const kb::scene::BehaviourComponent&,
        const kb::script::ScriptEvent& event,
        kb::script::ScriptExecutionContext& context) override {
        ++eventExecutionCount;
        eventSelf = context.Self();
        receivedEventName = event.name;
        receivedArgumentCount = context.EventArguments().size();
        return kb::script::ScriptBackendExecutionResult{.executed = true};
    }
};

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_script_runtime_tests";
}

void ResetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot(), error);
    kb::tests::Require(!error, "Script runtime test root could not be prepared");
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    kb::tests::Require(!error, "Script runtime test directory could not be created");

    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    kb::tests::Require(output.is_open(), "Script runtime test file could not be opened");
    output << text;
    kb::tests::Require(output.good(), "Script runtime test file could not be written");
}

void RunNativeScriptRuntimeDispatchTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Native Scripted"});
    constexpr kb::assets::AssetId kNativeAsset{1001U};
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();

    kb::scene::SceneEntity receivedSelf{};
    float receivedDelta = 0.0F;
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick, [&](kb::script::ScriptExecutionContext& context) {
                           receivedSelf = context.Self();
                           receivedDelta = context.DeltaSeconds();
                           context.Emit("NativeMoved", {
                                                           kb::script::ScriptEventArgument{
                                                               .name = "actor",
                                                               .value = kb::script::ScriptValue{context.Self().Id(), kb::script::ScriptValueType::Entity},
                                                           },
                                                       });
                       }),
        "Native script lifecycle registration failed");

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Native script backend registration failed");

    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.125F);
    kb::tests::Require(result.Succeeded(), "Native script runtime dispatch produced diagnostics");
    kb::tests::Require(result.visitedBehaviours == 1U, "Native script runtime did not visit the behaviour");
    kb::tests::Require(result.executedBehaviours == 1U, "Native script runtime did not execute the native behaviour");
    kb::tests::Require(receivedSelf == object.Entity(), "Native script runtime did not pass self");
    kb::tests::Require(receivedDelta == 0.125F, "Native script runtime did not pass delta seconds");
    kb::tests::Require(result.emittedEvents.size() == 1U && result.emittedEvents[0].name == "NativeMoved", "Native script runtime did not collect emitted events");
    kb::tests::Require(result.emittedEvents[0].arguments.size() == 1U, "Native script runtime did not preserve event payload");

    constexpr kb::assets::AssetId kSymbolChildAsset{ 1002U };
    const kb::scene::SceneObject symbolChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Native Symbol Child" });
    scene.Components().Behaviours().Set(symbolChild.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kSymbolChildAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });
    bool childSymbolExecuted = false;
    kb::tests::Require(native->RegisterLifecycleSymbol("tests.Native", kb::script::ScriptLifecycleEvent::Tick, [](kb::script::ScriptExecutionContext&) {}),
        "Native script symbol parent registration failed");
    kb::tests::Require(native->RegisterLifecycleSymbol("tests.Native:Child", kb::script::ScriptLifecycleEvent::Tick, [&childSymbolExecuted](kb::script::ScriptExecutionContext&) {
                           childSymbolExecuted = true;
                       }),
        "Native script symbol child registration failed");
    kb::tests::Require(native->BindAssetSymbol(kSymbolChildAsset, "tests.Native:Child"), "Native script symbol child asset binding failed");
    native->UnregisterSymbol("tests.Native");
    const kb::script::ScriptRuntimeExecutionResult symbolResult = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(symbolResult.Succeeded() && childSymbolExecuted, "Native script symbol unregister removed callbacks for a longer symbol with the same prefix");
}

void RunNativeScriptPluginManagerDispatchTest() {
    ResetTestRoot();
    const std::filesystem::path pluginPath = KB_NATIVE_SCRIPT_TEST_PLUGIN_PATH;
    kb::tests::Require(!pluginPath.empty() && std::filesystem::is_regular_file(pluginPath), "Native script test plugin DLL is missing");

    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Native Plugin Scripted" });
    constexpr kb::assets::AssetId kNativeAsset{ 1003U };
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    kb::script::ScriptRuntime runtime;
    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    kb::script::NativeScriptPluginManager plugins{ *native };
    const kb::script::NativeScriptPluginLoadResult loaded = plugins.LoadOrReload(kb::script::NativeScriptPluginLoadDesc{
        .key = "test-plugin",
        .modulePath = pluginPath,
        .shadowCopy = true,
        .shadowCopyDirectory = TestRoot() / "NativePluginShadow",
    });
    kb::tests::Require(loaded.Succeeded(), "Native script plugin manager did not load the test DLL");
    kb::tests::Require(plugins.IsLoaded("test-plugin"), "Native script plugin manager did not track the loaded DLL");
    kb::tests::Require(loaded.registeredSymbols.size() == 1U && loaded.registeredSymbols[0] == "tests.NativePlugin", "Native script plugin did not discover expected behaviour symbol");
    kb::tests::Require(native->BindAssetSymbol(kNativeAsset, "tests.NativePlugin"), "Native script plugin test asset symbol binding failed");

    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Native script plugin runtime backend registration failed");
    const kb::script::ScriptRuntimeExecutionResult tick = runtime.ExecuteLifecycleAndDispatchEvents(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(tick.Succeeded(), "Native script plugin runtime Tick produced diagnostics");
    const std::optional<kb::script::ScriptValue> tickValue = runtime.SharedState().Get("nativePlugin.Tick");
    kb::tests::Require(tickValue.has_value() && tickValue->AsInt() == 1, "Native script plugin Tick callback did not run");

    const kb::script::ScriptRuntimeExecutionResult event = runtime.DispatchEvent(scene, kb::script::ScriptEvent{
        .name = "NativePluginPing",
        .arguments = {
            kb::script::ScriptEventArgument{ .name = "value", .value = kb::script::ScriptValue{ 7 } },
        },
    }, 0.0F);
    kb::tests::Require(event.Succeeded() && event.executedBehaviours == 1U, "Native script plugin event callback did not run");
    const std::optional<kb::script::ScriptValue> eventValue = runtime.SharedState().Get("nativePlugin.Event");
    kb::tests::Require(eventValue.has_value() && eventValue->AsInt() == 1, "Native script plugin event callback did not preserve payload");

    const kb::script::NativeScriptPluginLoadResult failedReload = plugins.LoadOrReload(kb::script::NativeScriptPluginLoadDesc{
        .key = "test-plugin",
        .modulePath = pluginPath,
        .entryPoint = "kb_missing_native_script_entrypoint",
        .shadowCopy = true,
        .shadowCopyDirectory = TestRoot() / "NativePluginShadow",
    });
    kb::tests::Require(!failedReload.Succeeded(), "Native script plugin manager accepted a reload with a missing entry point");
    kb::tests::Require(plugins.IsLoaded("test-plugin"), "Native script plugin manager did not retain the previous plugin after a failed reload");
    const kb::script::ScriptRuntimeExecutionResult afterFailedReload = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(afterFailedReload.Succeeded() && afterFailedReload.executedBehaviours == 1U,
        "Native script plugin callback did not remain callable after a failed reload rollback");

    plugins.Unload("test-plugin");
    kb::tests::Require(!plugins.IsLoaded("test-plugin"), "Native script plugin manager did not unload the DLL");
    const kb::script::ScriptRuntimeExecutionResult afterUnload = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(afterUnload.executedBehaviours == 0U, "Native script plugin callbacks remained callable after unload");

    const kb::script::NativeScriptPluginLoadResult failedLoad = plugins.LoadOrReload(kb::script::NativeScriptPluginLoadDesc{
        .key = "partial-failure-plugin",
        .modulePath = pluginPath,
        .entryPoint = "kb_register_native_scripts_partial_failure",
        .shadowCopy = true,
        .shadowCopyDirectory = TestRoot() / "NativePluginShadow",
    });
    kb::tests::Require(!failedLoad.Succeeded(), "Native script plugin manager accepted a plugin whose registration returned false");
    kb::tests::Require(!plugins.IsLoaded("partial-failure-plugin"), "Native script plugin manager tracked a failed plugin load");
    kb::tests::Require(native->BindAssetSymbol(kNativeAsset, "tests.NativePluginPartialFailure"), "Native script partial failure asset symbol binding failed");
    const kb::script::ScriptRuntimeExecutionResult afterFailedLoad = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(afterFailedLoad.executedBehaviours == 0U, "Native script plugin manager left callbacks registered after failed plugin load");
}

void RunNativeScriptBuildPipelineTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "NativeBuildProject";
    const std::filesystem::path marker = projectRoot / "native_build.marker";
    std::error_code error;
    std::filesystem::create_directories(projectRoot, error);
    kb::tests::Require(!error, "Native script build test project directory could not be created");

    const kb::script::NativeScriptBuildResult built = kb::script::NativeScriptBuildPipeline::Build(kb::script::NativeScriptBuildDesc{
        .enabled = true,
        .command = std::string{ "cmake -E touch \"" } + marker.string() + "\"",
        .workingDirectory = projectRoot,
    });
    kb::tests::Require(built.Succeeded(), "Native script build pipeline did not execute the build command");
    kb::tests::Require(std::filesystem::is_regular_file(marker), "Native script build pipeline did not create the build marker");
}

void RunScriptRuntimeExecutionOrderTest() {
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kFirstAsset{ 1101U };
    constexpr kb::assets::AssetId kSecondAsset{ 1102U };
    constexpr kb::assets::AssetId kThirdAsset{ 1103U };

    const kb::scene::SceneObject first = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "First" });
    const kb::scene::SceneObject second = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Second" });
    const kb::scene::SceneObject third = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Third" });
    scene.Components().Behaviours().Set(first.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kFirstAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
        .tickGroup = kb::scene::BehaviourTickGroup::Camera,
        .executionOrder = -100,
    });
    scene.Components().Behaviours().Set(second.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kSecondAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
        .tickGroup = kb::scene::BehaviourTickGroup::Gameplay,
        .executionOrder = 50,
    });
    scene.Components().Behaviours().Set(third.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kThirdAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
        .tickGroup = kb::scene::BehaviourTickGroup::Gameplay,
        .executionOrder = -10,
    });

    std::vector<int> order;
    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    kb::tests::Require(native->RegisterLifecycle(kFirstAsset, kb::script::ScriptLifecycleEvent::Tick, [&order](kb::script::ScriptExecutionContext&) { order.push_back(1); }),
        "Script execution order first callback registration failed");
    kb::tests::Require(native->RegisterLifecycle(kSecondAsset, kb::script::ScriptLifecycleEvent::Tick, [&order](kb::script::ScriptExecutionContext&) { order.push_back(2); }),
        "Script execution order second callback registration failed");
    kb::tests::Require(native->RegisterLifecycle(kThirdAsset, kb::script::ScriptLifecycleEvent::Tick, [&order](kb::script::ScriptExecutionContext&) { order.push_back(3); }),
        "Script execution order third callback registration failed");

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Script execution order native backend registration failed");
    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);

    kb::tests::Require(result.Succeeded(), "Script execution order dispatch produced diagnostics");
    kb::tests::Require(order.size() == 3U, "Script execution order did not execute all behaviours");
    kb::tests::Require(order[0] == 3 && order[1] == 2 && order[2] == 1, "Script execution order did not sort by group and execution order");
}

void RunLuaScriptRuntimeDispatchTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Lua Scripted"});
    constexpr kb::assets::AssetId kLuaAsset{3001U};
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    FakeLuaRuntime fakeLua;
    auto luaBackend = std::make_unique<kb::script::LuaScriptBackend>(fakeLua);
    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::move(luaBackend)), "Lua script backend registration failed");

    const kb::script::ScriptRuntimeExecutionResult lifecycle = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.2F);
    kb::tests::Require(lifecycle.Succeeded(), "Lua script runtime lifecycle dispatch produced diagnostics");
    kb::tests::Require(lifecycle.visitedBehaviours == 1U, "Lua script runtime did not visit the behaviour");
    kb::tests::Require(lifecycle.executedBehaviours == 1U, "Lua script runtime did not execute through shared runtime");
    kb::tests::Require(fakeLua.lifecycleSelf == object.Entity(), "Lua script runtime did not receive self");
    kb::tests::Require(fakeLua.lifecycleDelta == 0.2F, "Lua script runtime did not receive delta seconds");
    kb::tests::Require(lifecycle.emittedEvents.size() == 1U && lifecycle.emittedEvents[0].name == "LuaReady", "Lua script runtime did not collect emitted events");

    const kb::script::ScriptEvent event{
        .name = "DoorOpened",
        .sender = object.Entity(),
        .senderAsset = kLuaAsset,
        .arguments = {
            kb::script::ScriptEventArgument{
                .name = "door",
                .value = kb::script::ScriptValue{object.Entity().Id(), kb::script::ScriptValueType::Entity},
            },
        },
    };
    const kb::script::ScriptRuntimeExecutionResult dispatched = runtime.DispatchEvent(scene, event, 0.25F);
    kb::tests::Require(dispatched.Succeeded(), "Lua script runtime event dispatch produced diagnostics");
    kb::tests::Require(dispatched.executedBehaviours == 1U, "Lua script runtime did not dispatch event through shared runtime");
    kb::tests::Require(fakeLua.eventSelf == object.Entity(), "Lua script runtime event dispatch did not receive self");
    kb::tests::Require(fakeLua.receivedEventName == "DoorOpened", "Lua script runtime event dispatch received the wrong event name");
    kb::tests::Require(fakeLua.receivedArgumentCount == 1U, "Lua script runtime event dispatch did not preserve typed payload");
}

void RunPucLuaScriptRuntimeDispatchTest() {
    kb::script::PucLuaScriptRuntime luaRuntime;
    constexpr kb::assets::AssetId kLuaAsset{3101U};
    const kb::script::PucLuaLoadResult loaded = luaRuntime.LoadScript(kLuaAsset, R"(
function Tick(self, dt)
    local wrote = self:SetProperty("Transform", "localPosition.x", 4.5)
    local x = self:GetProperty("Transform", "localPosition.x")
    Emit("LuaTicked", { entity = self.entity, delta = dt, x = x, hasTransform = self:HasComponent("Transform"), wrote = wrote })
end

function DoorOpened(self, event)
    Emit("LuaDoorHandled", { door = event.args.door, sender = event.sender })
end
)",
        "PlayerController.lua");
    kb::tests::Require(loaded.succeeded, "PUC Lua runtime did not load a valid script");
    kb::tests::Require(luaRuntime.HasScript(kLuaAsset), "PUC Lua runtime did not retain the loaded script");

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(luaRuntime)), "PUC Lua backend registration failed");

    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "PUC Lua Scripted"});
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    const kb::script::ScriptRuntimeExecutionResult tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.33F);
    kb::tests::Require(tick.Succeeded(), "PUC Lua runtime Tick produced diagnostics");
    kb::tests::Require(tick.executedBehaviours == 1U, "PUC Lua runtime did not execute Tick");
    kb::tests::Require(tick.emittedEvents.size() == 1U && tick.emittedEvents[0].name == "LuaTicked", "PUC Lua runtime did not emit Tick event");
    kb::tests::Require(tick.emittedEvents[0].arguments.size() == 5U, "PUC Lua runtime did not preserve Tick payload");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(object.Entity()).localPosition.x, 4.5F), "PUC Lua self:SetProperty did not mutate Transform");

    const kb::script::ScriptEvent doorOpened{
        .name = "DoorOpened",
        .sender = object.Entity(),
        .senderAsset = kLuaAsset,
        .arguments = {
            kb::script::ScriptEventArgument{
                .name = "door",
                .value = kb::script::ScriptValue{object.Entity().Id(), kb::script::ScriptValueType::Entity},
            },
        },
    };
    const kb::script::ScriptRuntimeExecutionResult event = runtime.DispatchEvent(scene, doorOpened, 0.0F);
    kb::tests::Require(event.Succeeded(), "PUC Lua runtime custom event produced diagnostics");
    kb::tests::Require(event.executedBehaviours == 1U, "PUC Lua runtime did not execute custom event");
    kb::tests::Require(event.emittedEvents.size() == 1U && event.emittedEvents[0].name == "LuaDoorHandled", "PUC Lua runtime did not emit custom event response");
    kb::tests::Require(event.emittedEvents[0].arguments.size() == 2U, "PUC Lua runtime did not preserve custom event payload");

    const kb::script::PucLuaLoadResult failed = luaRuntime.LoadScript(kb::assets::AssetId{3102U}, "function Broken(", "Broken.lua");
    kb::tests::Require(!failed.succeeded && !failed.error.empty(), "PUC Lua runtime accepted an invalid script");

    constexpr kb::assets::AssetId kSandboxAsset{3103U};
    const kb::script::PucLuaLoadResult sandboxLoaded = luaRuntime.LoadScript(kSandboxAsset, R"(
function Tick(self, dt)
    if os ~= nil or io ~= nil or package ~= nil or debug ~= nil or dofile ~= nil or loadfile ~= nil or load ~= nil or collectgarbage ~= nil then
        Emit("UnsafeLibraryVisible")
    end
    if _G == nil or _G._G ~= _G or _G.os ~= nil or _G.io ~= nil or _G.package ~= nil or _G.debug ~= nil then
        Emit("UnsafeLibraryVisible")
    end
end
)",
        "Sandbox.lua");
    kb::tests::Require(sandboxLoaded.succeeded, "PUC Lua runtime did not load sandbox test script");
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kSandboxAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    const kb::script::ScriptRuntimeExecutionResult sandbox = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(sandbox.Succeeded(), "PUC Lua sandbox test produced diagnostics");
    kb::tests::Require(sandbox.emittedEvents.empty(), "PUC Lua runtime exposed unsafe standard libraries");

    constexpr kb::assets::AssetId kSandboxPolluterAsset{3104U};
    constexpr kb::assets::AssetId kSandboxCheckerAsset{3105U};
    kb::tests::Require(luaRuntime.LoadScript(kSandboxPolluterAsset, R"(
function Tick(self, dt)
    math.__kb_polluted = true
end
)", "SandboxPolluter.lua").succeeded,
        "PUC Lua sandbox polluter script did not load");
    kb::tests::Require(luaRuntime.LoadScript(kSandboxCheckerAsset, R"(
function Tick(self, dt)
    if math.__kb_polluted ~= nil then
        Emit("SandboxLibraryMutationLeaked")
    end
end
)", "SandboxChecker.lua").succeeded,
        "PUC Lua sandbox checker script did not load");
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kSandboxPolluterAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    kb::tests::Require(runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F).Succeeded(), "PUC Lua sandbox polluter execution failed");
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kSandboxCheckerAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    const kb::script::ScriptRuntimeExecutionResult sandboxLeak = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(sandboxLeak.Succeeded() && sandboxLeak.emittedEvents.empty(), "PUC Lua sandbox leaked library table mutations across script environments");
}

void RunPucLuaScriptRuntimeModulesReloadAndDiagnosticsTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lua Runtime Advanced" });
    constexpr kb::assets::AssetId kLuaAsset{ 3201U };
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    kb::script::PucLuaScriptRuntime luaRuntime;
    const kb::script::PucLuaLoadResult module = luaRuntime.RegisterModule("Shared.Math", R"(
local M = {}
function M.add(a, b)
    return a + b
end
return M
)",
        "Shared/Math.lua");
    kb::tests::Require(module.succeeded && luaRuntime.HasModule("Shared.Math"), "PUC Lua runtime did not register importable module");

    const kb::script::PucLuaLoadResult loaded = luaRuntime.LoadScript(kLuaAsset, R"(
local Math = Import("Shared.Math")
function Tick(self, dt)
    SetShared("lua.module.sum", Math.add(2, 5))
end
)",
        "Player.lua",
        100U);
    kb::tests::Require(loaded.succeeded && luaRuntime.IsScriptCurrent(kLuaAsset, 100U), "PUC Lua runtime did not load script with module import");

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(luaRuntime)), "PUC Lua advanced backend registration failed");

    kb::script::ScriptRuntimeExecutionResult tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(tick.Succeeded(), "PUC Lua module import execution produced diagnostics");
    std::optional<kb::script::ScriptValue> sum = runtime.SharedState().Get("lua.module.sum");
    kb::tests::Require(sum.has_value() && sum->AsInt() == 7, "PUC Lua module import did not return callable module table");

    const kb::script::PucLuaLoadResult reloaded = luaRuntime.ReloadScript(kLuaAsset, R"(
local Math = Import("Shared.Math")
function Tick(self, dt)
    SetShared("lua.module.sum", Math.add(10, 5))
end
)",
        "Player.lua",
        101U);
    kb::tests::Require(reloaded.succeeded && luaRuntime.IsScriptCurrent(kLuaAsset, 101U), "PUC Lua hot reload did not replace the script state");

    tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(tick.Succeeded(), "PUC Lua hot reloaded script produced diagnostics");
    sum = runtime.SharedState().Get("lua.module.sum");
    kb::tests::Require(sum.has_value() && sum->AsInt() == 15, "PUC Lua hot reload did not execute the replacement script");

    const kb::script::PucLuaLoadResult failedReload = luaRuntime.ReloadScript(kLuaAsset, "function Broken(", "Player.lua", 102U);
    kb::tests::Require(!failedReload.succeeded && luaRuntime.IsScriptCurrent(kLuaAsset, 101U), "PUC Lua failed reload replaced the last valid script");
    tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(tick.Succeeded(), "PUC Lua script did not remain executable after failed reload");
    sum = runtime.SharedState().Get("lua.module.sum");
    kb::tests::Require(sum.has_value() && sum->AsInt() == 15, "PUC Lua failed reload did not preserve the last valid state");

    kb::tests::Require(luaRuntime.RegisterModule("Cycle.A", "local B, err = Import(\"Cycle.B\")\nif not B then error(err) end\nreturn {}\n", "Cycle/A.lua").succeeded,
        "PUC Lua circular import test module A did not register");
    kb::tests::Require(luaRuntime.RegisterModule("Cycle.B", "local A, err = Import(\"Cycle.A\")\nif not A then error(err) end\nreturn {}\n", "Cycle/B.lua").succeeded,
        "PUC Lua circular import test module B did not register");
    const kb::script::PucLuaLoadResult cycleLoaded = luaRuntime.LoadScript(kLuaAsset, R"(
function Tick(self, dt)
    local A, err = Import("Cycle.A")
    if not A then
        error(err)
    end
end
)",
        "CycleUser.lua",
        106U);
    kb::tests::Require(cycleLoaded.succeeded, "PUC Lua circular import user script did not load");
    tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(!tick.Succeeded() && !tick.diagnostics.empty() &&
            tick.diagnostics.front().message.find("lua circular module import detected") != std::string::npos,
        "PUC Lua runtime did not diagnose circular module imports");

    luaRuntime.SetDebugSettings(kb::script::PucLuaDebugSettings{
        .enableBreakpoints = true,
        .breakpoints = {
            kb::script::PucLuaDebugBreakpoint{ .chunkName = "Debug.lua", .line = 2, .enabled = true },
        },
    });
    const kb::script::PucLuaLoadResult debugLoaded = luaRuntime.LoadScript(kLuaAsset, "function Tick(self, dt)\n    SetShared(\"lua.debug.hit\", 1)\nend\n", "Debug.lua", 103U);
    kb::tests::Require(debugLoaded.succeeded, "PUC Lua debug test script did not load");
    tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(!tick.Succeeded() && !tick.diagnostics.empty() && tick.diagnostics.front().message.find("lua breakpoint hit") != std::string::npos,
        "PUC Lua breakpoint did not produce a runtime diagnostic");
    const kb::script::PucLuaDebugPauseSnapshot& pause = luaRuntime.LastDebugPause();
    kb::tests::Require(pause.valid && pause.reason == kb::script::PucLuaDebugPauseReason::Breakpoint && pause.chunkName.ends_with("Debug.lua") && pause.line == 2,
        "PUC Lua debugger did not record breakpoint pause metadata");
    kb::tests::Require(!pause.callStack.empty(), "PUC Lua debugger did not capture call stack");

    luaRuntime.SetDebugSettings(kb::script::PucLuaDebugSettings{});
    const kb::script::PucLuaLoadResult errorLoaded = luaRuntime.LoadScript(kLuaAsset, R"(function Tick(self, dt)
    local function fail()
        error("lua diagnostics boom")
    end
    fail()
end
)",
        "Traceback.lua",
        104U);
    kb::tests::Require(errorLoaded.succeeded, "PUC Lua traceback test script did not load");
    tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(!tick.Succeeded() && !tick.diagnostics.empty(), "PUC Lua runtime error did not produce diagnostics");
    kb::tests::Require(tick.diagnostics.front().message.find("lua diagnostics boom") != std::string::npos &&
            tick.diagnostics.front().message.find("stack traceback") != std::string::npos,
        "PUC Lua runtime error did not include an enriched stack trace");

    const kb::script::PucLuaLoadResult manualBreakLoaded = luaRuntime.LoadScript(kLuaAsset, "function Tick(self, dt)\n    SetShared(\"lua.debug.manual\", 1)\nend\n", "ManualBreak.lua", 105U);
    kb::tests::Require(manualBreakLoaded.succeeded, "PUC Lua manual debug break script did not load");
    luaRuntime.SetDebugSettings(kb::script::PucLuaDebugSettings{ .enableBreakpoints = false });
    luaRuntime.ClearDebugPause();
    luaRuntime.RequestBreakOnNextLine();
    tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(!tick.Succeeded(), "PUC Lua manual break did not pause execution");
    const kb::script::PucLuaDebugPauseSnapshot& manualPause = luaRuntime.LastDebugPause();
    kb::tests::Require(manualPause.valid && manualPause.reason == kb::script::PucLuaDebugPauseReason::ManualBreak && manualPause.chunkName.ends_with("ManualBreak.lua"),
        "PUC Lua debugger did not record manual break pause metadata");
}

void RunLuaExposedVariablesRuntimeTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "LuaVariablesProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "Mover.lua", R"(-- @expose speed Float = 2.5
-- @expose label String = Runner
function Tick(self, dt)
    local speed = self.variables.speed
    local label = self:GetVariable("label")
    self:SetVariable("speed", speed + 1.0)
    Emit("LuaVariablesTick", { speed = speed, label = label, updated = self:GetVariable("speed") })
end
)");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Lua exposed variables project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Lua exposed variables asset discovery failed");
    const kb::assets::AssetMetadata* luaMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Mover.lua");
    kb::tests::Require(luaMetadata != nullptr, "Lua exposed variables metadata was not discovered");

    kb::script::PucLuaScriptRuntime luaRuntime;
    kb::visual::VisualGraphRuntimeRegistry visualArtifacts;
    kb::script::ScriptRuntimeAssetPreparer preparer{ scene.Assets().Manager(), luaRuntime, visualArtifacts };

    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lua Variables" });
    const kb::script::ScriptBehaviourBindingResult bound = kb::script::ScriptBehaviourBindingService::AttachMetadata(scene, object.Entity(), *luaMetadata, {}, &preparer);
    kb::tests::Require(bound.Succeeded(), "Lua exposed variables behaviour binding failed");

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(luaRuntime)), "Lua exposed variables backend registration failed");
    const kb::script::ScriptRuntimeExecutionResult tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(tick.Succeeded() && tick.emittedEvents.size() == 1U, "Lua exposed variables script did not execute");
    bool sawInitialSpeed = false;
    bool sawLabel = false;
    bool sawUpdatedSpeed = false;
    for (const kb::script::ScriptEventArgument& argument : tick.emittedEvents.front().arguments) {
        sawInitialSpeed = sawInitialSpeed || (argument.name == "speed" && kb::tests::NearlyEqual(argument.value.AsFloat(), 2.5F));
        sawLabel = sawLabel || (argument.name == "label" && argument.value.AsString() == "Runner");
        sawUpdatedSpeed = sawUpdatedSpeed || (argument.name == "updated" && kb::tests::NearlyEqual(argument.value.AsFloat(), 3.5F));
    }
    kb::tests::Require(sawInitialSpeed && sawLabel && sawUpdatedSpeed, "Lua exposed variables were not visible through self variables API");
    const std::span<const kb::script::PucLuaExposedVariableInstance> variables = luaRuntime.InstanceVariables(object.Entity(), luaMetadata->id);
    const auto updatedSpeedVariable = std::ranges::find_if(variables, [](const kb::script::PucLuaExposedVariableInstance& variable) { return variable.name == "speed"; });
    const auto labelVariable = std::ranges::find_if(variables, [](const kb::script::PucLuaExposedVariableInstance& variable) { return variable.name == "label"; });
    kb::tests::Require(updatedSpeedVariable != variables.end() && updatedSpeedVariable->type == kb::script::ScriptValueType::Float &&
            kb::tests::NearlyEqual(updatedSpeedVariable->value.AsFloat(), 3.5F) && updatedSpeedVariable->overridden,
        "Lua exposed variable setter did not persist override in runtime instance store");
    kb::tests::Require(labelVariable != variables.end() && labelVariable->type == kb::script::ScriptValueType::String &&
            labelVariable->value.AsString() == "Runner",
        "Lua exposed variable default was not retained in runtime instance store");

    WriteTextFile(assetsRoot / "Logic" / "Mover.lua", R"(-- @expose speed Float = 9.5
-- @expose label String = Walker
function Tick(self, dt)
    local speed = self.variables.speed
    local label = self:GetVariable("label")
    self:SetVariable("speed", speed + 1.0)
    Emit("LuaVariablesTick", { speed = speed, label = label, updated = self:GetVariable("speed") })
end
)");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Lua exposed variables rediscovery failed");
    const kb::script::ScriptRuntimeAssetPrepareResult reloaded = preparer.PrepareBehaviour(*scene.Components().Behaviours().TryGet(object.Entity()));
    kb::tests::Require(reloaded.Succeeded(), "Lua exposed variables asset reload produced diagnostics");
    const kb::script::ScriptRuntimeExecutionResult tickAfterDefaultReload = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(tickAfterDefaultReload.Succeeded() && tickAfterDefaultReload.emittedEvents.size() == 1U,
        "Lua exposed variables script did not execute after default reload");
    bool sawReloadedLabel = false;
    bool sawRetainedOverride = false;
    for (const kb::script::ScriptEventArgument& argument : tickAfterDefaultReload.emittedEvents.front().arguments) {
        sawReloadedLabel = sawReloadedLabel || (argument.name == "label" && argument.value.AsString() == "Walker");
        sawRetainedOverride = sawRetainedOverride || (argument.name == "speed" && kb::tests::NearlyEqual(argument.value.AsFloat(), 3.5F));
    }
    kb::tests::Require(sawReloadedLabel && sawRetainedOverride,
        "Lua exposed variables did not refresh non-overridden defaults while preserving explicit overrides");

    const kb::script::ScriptRuntimeExecutionResult destroyed = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Destroyed, 0.0F);
    kb::tests::Require(destroyed.Succeeded(), "Lua exposed variables Destroyed lifecycle produced diagnostics");
    kb::tests::Require(luaRuntime.InstanceVariables(object.Entity(), luaMetadata->id).empty(), "Lua exposed variables instance store was not cleared after Destroyed lifecycle");

    WriteTextFile(assetsRoot / "Logic" / "Broken.lua", "function Broken(\n");
    kb::tests::Require(scene.Assets().Discover() == 2U, "Lua broken binding rediscovery failed");
    const kb::assets::AssetMetadata* brokenMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Broken.lua");
    kb::tests::Require(brokenMetadata != nullptr, "Lua broken binding metadata was not discovered");
    const kb::scene::SceneObject brokenObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Broken Lua Binding" });
    const kb::script::ScriptBehaviourBindingResult brokenBound = kb::script::ScriptBehaviourBindingService::AttachMetadata(scene, brokenObject.Entity(), *brokenMetadata, {}, &preparer);
    kb::tests::Require(!brokenBound.Succeeded(), "Script behaviour binding succeeded for a broken Lua script");
    kb::tests::Require(scene.Components().Behaviours().TryGet(brokenObject.Entity()) == nullptr, "Script behaviour binding left a component attached after prepare failure");
}

void RunCrossBackendEventDispatchTest() {
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kNativeAsset{4001U};
    constexpr kb::assets::AssetId kLuaAsset{4002U};
    const kb::scene::SceneObject nativeObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Native Sender"});
    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Lua Listener"});
    scene.Components().Behaviours().Set(nativeObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });
    scene.Components().Behaviours().Set(luaObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick, [](kb::script::ScriptExecutionContext& context) {
                           context.Emit("DoorOpened", {
                                                          kb::script::ScriptEventArgument{
                                                              .name = "door",
                                                              .value = kb::script::ScriptValue{context.Self().Id(), kb::script::ScriptValueType::Entity},
                                                          },
                                                      });
                       }),
        "Native cross-backend event registration failed");

    FakeLuaRuntime fakeLua;
    fakeLua.emitLifecycleEvent = false;
    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Native backend registration failed for cross-backend event test");
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(fakeLua)), "Lua backend registration failed for cross-backend event test");

    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycleAndDispatchEvents(scene, kb::script::ScriptLifecycleEvent::Tick, 0.1F);
    kb::tests::Require(result.Succeeded(), "Cross-backend event dispatch produced diagnostics");
    kb::tests::Require(result.emittedEvents.size() == 1U && result.emittedEvents[0].name == "DoorOpened", "Cross-backend event dispatch did not retain emitted event history");
    kb::tests::Require(fakeLua.eventSelf == luaObject.Entity(), "Cross-backend event dispatch did not deliver the event to Lua backend");
    kb::tests::Require(fakeLua.receivedEventName == "DoorOpened", "Cross-backend event dispatch delivered the wrong event name");
    kb::tests::Require(fakeLua.receivedArgumentCount == 1U, "Cross-backend event dispatch did not preserve payload");
}

void RunTargetedEventDispatchTest() {
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kNativeAsset{4101U};
    constexpr kb::assets::AssetId kLuaAsset{4102U};
    const kb::scene::SceneObject nativeObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Native Target Sender"});
    const kb::scene::SceneObject targetLuaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Target Lua Listener"});
    const kb::scene::SceneObject otherLuaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Other Lua Listener"});
    scene.Components().Behaviours().Set(nativeObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });
    scene.Components().Behaviours().Set(targetLuaObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    scene.Components().Behaviours().Set(otherLuaObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick, [target = targetLuaObject.Entity()](kb::script::ScriptExecutionContext& context) {
                           context.EmitTo(target, "PrivateMessage", {
                                                               kb::script::ScriptEventArgument{
                                                                   .name = "value",
                                                                   .value = kb::script::ScriptValue{7},
                                                               },
                                                           });
                       }),
        "Targeted event native callback registration failed");

    FakeLuaRuntime fakeLua;
    fakeLua.emitLifecycleEvent = false;
    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Targeted event native backend registration failed");
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(fakeLua)), "Targeted event Lua backend registration failed");

    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycleAndDispatchEvents(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(result.Succeeded(), "Targeted event dispatch produced diagnostics");
    kb::tests::Require(result.emittedEvents.size() == 1U && result.emittedEvents[0].target == targetLuaObject.Entity(), "Targeted event dispatch did not retain target metadata");
    kb::tests::Require(fakeLua.eventExecutionCount == 1U, "Targeted event dispatch did not filter non-target behaviours");
    kb::tests::Require(fakeLua.eventSelf == targetLuaObject.Entity(), "Targeted event dispatch delivered to the wrong entity");
    kb::tests::Require(fakeLua.receivedEventName == "PrivateMessage" && fakeLua.receivedArgumentCount == 1U, "Targeted event dispatch did not preserve event payload");
}

void RunCrossBackendSharedStateTest() {
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kNativeAsset{3301U};
    constexpr kb::assets::AssetId kLuaAsset{3302U};
    constexpr kb::assets::AssetId kVisualAsset{3303U};
    const kb::scene::SceneObject nativeObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Native Shared"});
    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Lua Shared"});
    const kb::scene::SceneObject graphObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Graph Shared"});
    scene.Components().Behaviours().Set(nativeObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
        .executionOrder = 0,
    });
    scene.Components().Behaviours().Set(luaObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
        .executionOrder = 10,
    });
    scene.Components().Behaviours().Set(graphObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kVisualAsset.value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
        .executionOrder = 20,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick, [](kb::script::ScriptExecutionContext& context) {
                           kb::tests::Require(context.SetSharedValue("nativeScore", kb::script::ScriptValue{40}), "Native script could not set shared state");
                       }),
        "Cross-backend shared state native callback registration failed");

    kb::script::PucLuaScriptRuntime luaRuntime;
    const kb::script::PucLuaLoadResult loadedLua = luaRuntime.LoadScript(kLuaAsset, R"(
function Tick(self, dt)
    local score = GetShared("nativeScore")
    SetShared("luaScore", score + 5)
    Emit("LuaSharedDone", { score = score })
end
)");
    kb::tests::Require(loadedLua.succeeded, "Cross-backend shared state Lua script did not load");

    kb::visual::VisualGraphAsset graph{};
    graph.name = "SharedStateGraph";
    graph.nodes = {
        kb::visual::VisualGraphNode{.id = 1U, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick},
        kb::visual::VisualGraphNode{.id = 2U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "LuaScoreKey"},
        kb::visual::VisualGraphNode{.id = 3U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "GraphScoreKey"},
        kb::visual::VisualGraphNode{.id = 4U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "Shared.Get.Int"},
        kb::visual::VisualGraphNode{.id = 5U, .kind = kb::visual::VisualGraphNodeKind::SetProperty, .symbol = "Shared.Set.Int"},
        kb::visual::VisualGraphNode{.id = 6U, .kind = kb::visual::VisualGraphNodeKind::EmitEvent, .symbol = "GraphSharedDone"},
    };
    graph.pins = {
        kb::visual::VisualGraphPin{.nodeId = 1U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::String},
        kb::visual::VisualGraphPin{.nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::String},
        kb::visual::VisualGraphPin{.nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "key", .type = kb::visual::VisualGraphValueType::String},
        kb::visual::VisualGraphPin{.nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::Int},
        kb::visual::VisualGraphPin{.nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "key", .type = kb::visual::VisualGraphValueType::String},
        kb::visual::VisualGraphPin{.nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "value", .type = kb::visual::VisualGraphValueType::Int},
        kb::visual::VisualGraphPin{.nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool},
        kb::visual::VisualGraphPin{.nodeId = 6U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{.fromNode = 1U, .fromPin = "then", .toNode = 5U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution},
        kb::visual::VisualGraphEdge{.fromNode = 5U, .fromPin = "then", .toNode = 6U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution},
        kb::visual::VisualGraphEdge{.fromNode = 2U, .fromPin = "value", .toNode = 4U, .toPin = "key", .kind = kb::visual::VisualGraphEdgeKind::Data},
        kb::visual::VisualGraphEdge{.fromNode = 3U, .fromPin = "value", .toNode = 5U, .toPin = "key", .kind = kb::visual::VisualGraphEdgeKind::Data},
        kb::visual::VisualGraphEdge{.fromNode = 4U, .fromPin = "value", .toNode = 5U, .toPin = "value", .kind = kb::visual::VisualGraphEdgeKind::Data},
    };
    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Cross-backend shared state graph did not compile");
    kb::visual::VisualGraphRuntimeRegistry artifacts;
    artifacts.Store(kb::visual::VisualGraphRuntimeArtifact{
        .assetId = kVisualAsset,
        .graphName = graph.name,
        .module = compiled.module,
    });

    kb::script::ScriptRuntime runtime;
    kb::visual::VisualGraphRuntimeBindingRegistry graphBindings;
    kb::tests::Require(kb::script::ScriptSharedVisualGraphBindings::Register(graphBindings, runtime.SharedState()), "Cross-backend shared state graph bindings did not register");
    kb::tests::Require(graphBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "LuaScoreKey",
                           .outputs = {kb::visual::VisualGraphPinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::String}},
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{std::string{"luaScore"}});
                           },
                       }),
        "Cross-backend shared state LuaScoreKey binding did not register");
    kb::tests::Require(graphBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "GraphScoreKey",
                           .outputs = {kb::visual::VisualGraphPinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::String}},
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{std::string{"graphScore"}});
                           },
                       }),
        "Cross-backend shared state GraphScoreKey binding did not register");
    kb::visual::VisualGraphBehaviourInstanceRegistry graphInstances;
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Cross-backend shared state native backend did not register");
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(luaRuntime)), "Cross-backend shared state Lua backend did not register");
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::VisualGraphScriptBackend>(artifacts, graphBindings, graphInstances)),
        "Cross-backend shared state VisualGraph backend did not register");

    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycleAndDispatchEvents(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(result.Succeeded(), "Cross-backend shared state execution produced diagnostics");
    const std::optional<kb::script::ScriptValue> nativeScore = runtime.SharedState().Get("nativeScore");
    const std::optional<kb::script::ScriptValue> luaScore = runtime.SharedState().Get("luaScore");
    const std::optional<kb::script::ScriptValue> graphScore = runtime.SharedState().Get("graphScore");
    kb::tests::Require(nativeScore.has_value() && nativeScore->AsInt() == 40, "Native shared value was not stored");
    kb::tests::Require(luaScore.has_value() && luaScore->AsInt() == 45, "Lua did not read and update shared state");
    kb::tests::Require(graphScore.has_value() && graphScore->AsInt() == 45, "VisualGraph did not read Lua shared state and write graph shared state");

    bool sawLuaEvent = false;
    bool sawGraphEvent = false;
    for (const kb::script::ScriptEvent& event : result.emittedEvents) {
        sawLuaEvent = sawLuaEvent || event.name == "LuaSharedDone";
        sawGraphEvent = sawGraphEvent || event.name == "GraphSharedDone";
    }
    kb::tests::Require(sawLuaEvent && sawGraphEvent, "Cross-backend shared state did not preserve cross-language event dispatch");
}

void RunScriptFunctionRegistryCrossBackendTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script function host setup failed");

    int inventoryTotal = 0;
    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{
                               .name = "Inventory.AddItem",
                               .inputs = {
                                   kb::script::ScriptFunctionPin{ .name = "itemId", .type = kb::script::ScriptValueType::Int },
                               },
                               .outputs = {
                                   kb::script::ScriptFunctionPin{ .name = "total", .type = kb::script::ScriptValueType::Int },
                               },
                           },
                           .callback = [&inventoryTotal](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument> arguments) {
                               inventoryTotal += arguments[0].value.AsInt();
                               return kb::script::ScriptFunctionCallResult{
                                   .executed = true,
                                   .outputs = {
                                       kb::script::ScriptFunctionArgument{
                                           .name = "total",
                                           .value = kb::script::ScriptValue{ inventoryTotal },
                                       },
                                   },
                               };
                           },
                       }),
        "Script function registry did not register Inventory.AddItem");
    kb::tests::Require(host.VisualGraphRuntimeBindings().Find(kb::visual::VisualGraphIrOpcode::CallNative, "Function.Inventory.AddItem") != nullptr,
        "Script function registry did not expose runtime VisualGraph binding");
    kb::tests::Require(host.VisualGraphNativeBindings().Find(kb::visual::VisualGraphIrOpcode::CallNative, "Function.Inventory.AddItem") != nullptr,
        "Script function registry did not expose native VisualGraph binding");
    kb::tests::Require(host.CreateVisualGraphNodeCatalog().Find("NativeBinding:CallNative:Function.Inventory.AddItem") != nullptr,
        "Script function registry did not expose VisualGraph catalog entry");
    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{
                               .name = "Entity.Use",
                               .inputs = {
                                   kb::script::ScriptFunctionPin{ .name = "target", .type = kb::script::ScriptValueType::Entity },
                               },
                           },
                           .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                               return kb::script::ScriptFunctionCallResult{ .executed = true };
                           },
                       }),
        "Script function registry did not register Entity.Use");
    const std::array negativeEntityArguments{
        kb::script::ScriptFunctionArgument{ .name = "target", .value = kb::script::ScriptValue{ -1 } },
    };
    const kb::script::ScriptFunctionCallResult negativeEntityCall = host.Functions().Call(
        "Entity.Use",
        std::span<const kb::script::ScriptFunctionArgument>{ negativeEntityArguments },
        kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(!negativeEntityCall.Succeeded(), "Script function registry accepted a negative Int as Entity");
    const std::array extraInputArguments{
        kb::script::ScriptFunctionArgument{ .name = "itemId", .value = kb::script::ScriptValue{ 1 } },
        kb::script::ScriptFunctionArgument{ .name = "typo", .value = kb::script::ScriptValue{ 2 } },
    };
    const kb::script::ScriptFunctionCallResult extraInputCall = host.Functions().Call(
        "Inventory.AddItem",
        std::span<const kb::script::ScriptFunctionArgument>{ extraInputArguments },
        kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(!extraInputCall.Succeeded(), "Script function registry ignored an unknown input argument");
    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{
                               .name = "Inventory.BadOutput",
                               .outputs = {
                                   kb::script::ScriptFunctionPin{ .name = "total", .type = kb::script::ScriptValueType::Int },
                               },
                           },
                           .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                               return kb::script::ScriptFunctionCallResult{
                                   .executed = true,
                                   .outputs = {
                                       kb::script::ScriptFunctionArgument{ .name = "total", .value = kb::script::ScriptValue{ 1 } },
                                       kb::script::ScriptFunctionArgument{ .name = "unexpected", .value = kb::script::ScriptValue{ 2 } },
                                   },
                               };
                           },
                       }),
        "Script function registry did not register Inventory.BadOutput");
    const kb::script::ScriptFunctionCallResult unknownOutputCall = host.Functions().Call("Inventory.BadOutput", {}, kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(!unknownOutputCall.Succeeded(), "Script function registry ignored an unknown output argument");

    constexpr kb::assets::AssetId kNativeAsset{ 5010U };
    constexpr kb::assets::AssetId kLuaAsset{ 5011U };
    constexpr kb::assets::AssetId kVisualAsset{ 5012U };
    const kb::scene::SceneObject nativeObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Native Function Caller" });
    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lua Function Caller" });
    const kb::scene::SceneObject graphObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Visual Function Caller" });
    scene.Components().Behaviours().Set(nativeObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
        .executionOrder = 0,
    });
    scene.Components().Behaviours().Set(luaObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
        .executionOrder = 10,
    });
    scene.Components().Behaviours().Set(graphObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kVisualAsset.value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
        .executionOrder = 20,
    });

    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick, [](kb::script::ScriptExecutionContext& context) {
                           const std::vector<kb::script::ScriptFunctionArgument> arguments{
                               kb::script::ScriptFunctionArgument{ .name = "itemId", .value = kb::script::ScriptValue{ 2 } },
                           };
                           const kb::script::ScriptFunctionCallResult result = context.CallFunction("Inventory.AddItem", arguments);
                           kb::tests::Require(result.Succeeded(), "Native function call failed");
                           const std::optional<kb::script::ScriptValue> total = result.Output("total");
                           kb::tests::Require(total.has_value(), "Native function call did not return total");
                           kb::tests::Require(context.SetSharedValue("nativeFunctionTotal", *total), "Native function call could not store shared result");
                       }),
        "Native script function caller did not register");

    const kb::script::PucLuaLoadResult loadedLua = host.LuaRuntime().LoadScript(kLuaAsset, R"(
function Tick(self, dt)
    local total = CallFunction("Inventory.AddItem", { itemId = 3 })
    SetShared("luaFunctionTotal", total)
end
)");
    kb::tests::Require(loadedLua.succeeded, "Lua function caller did not load");

    kb::visual::VisualGraphAsset graph{};
    graph.name = "VisualFunctionCaller";
    graph.nodes = {
        kb::visual::VisualGraphNode{ .id = 1U, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick },
        kb::visual::VisualGraphNode{ .id = 2U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "GraphFunctionItemId" },
        kb::visual::VisualGraphNode{ .id = 3U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "GraphFunctionTotalKey" },
        kb::visual::VisualGraphNode{ .id = 4U, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "Function.Inventory.AddItem" },
        kb::visual::VisualGraphNode{ .id = 5U, .kind = kb::visual::VisualGraphNodeKind::SetProperty, .symbol = "Shared.Set.Int" },
    };
    graph.pins = {
        kb::visual::VisualGraphPin{ .nodeId = 1U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::Int },
        kb::visual::VisualGraphPin{ .nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "itemId", .type = kb::visual::VisualGraphValueType::Int },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "total", .type = kb::visual::VisualGraphValueType::Int },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "key", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "value", .type = kb::visual::VisualGraphValueType::Int },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool },
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{ .fromNode = 1U, .fromPin = "then", .toNode = 4U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
        kb::visual::VisualGraphEdge{ .fromNode = 4U, .fromPin = "then", .toNode = 5U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
        kb::visual::VisualGraphEdge{ .fromNode = 2U, .fromPin = "value", .toNode = 4U, .toPin = "itemId", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 3U, .fromPin = "value", .toNode = 5U, .toPin = "key", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 4U, .fromPin = "total", .toNode = 5U, .toPin = "value", .kind = kb::visual::VisualGraphEdgeKind::Data },
    };
    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Visual function caller graph did not compile");
    const kb::visual::VisualGraphNativeCode generated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "VisualFunctionCaller",
        .namespaceName = "kb::game",
        .bindings = &host.VisualGraphNativeBindings(),
    });
    kb::tests::Require(generated.Succeeded(), "Visual function caller native code did not generate");
    kb::tests::Require(generated.source.find("context.CallFunction(\"Inventory.AddItem\"") != std::string::npos,
        "Visual function caller native code did not emit direct script function call");
    kb::tests::Require(generated.source.find("context.CallNative(\"Function.Inventory.AddItem\")") == std::string::npos,
        "Visual function caller native code used fallback CallNative for script function");
    host.VisualGraphs().Store(kb::visual::VisualGraphRuntimeArtifact{
        .assetId = kVisualAsset,
        .graphName = graph.name,
        .module = compiled.module,
        .nativeCode = generated,
    });
    kb::tests::Require(host.VisualGraphRuntimeBindings().Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "GraphFunctionItemId",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Int } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ 4 });
                           },
                       }),
        "Visual function item id binding did not register");
    kb::tests::Require(host.VisualGraphRuntimeBindings().Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "GraphFunctionTotalKey",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::String } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ std::string{ "graphFunctionTotal" } });
                           },
                       }),
        "Visual function total key binding did not register");

    const kb::script::ScriptRuntimeExecutionResult result = host.Runtime().ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(result.Succeeded(), "Script function cross-backend runtime produced diagnostics");
    const std::optional<kb::script::ScriptValue> nativeTotal = host.SharedState().Get("nativeFunctionTotal");
    const std::optional<kb::script::ScriptValue> luaTotal = host.SharedState().Get("luaFunctionTotal");
    const std::optional<kb::script::ScriptValue> graphTotal = host.SharedState().Get("graphFunctionTotal");
    kb::tests::Require(nativeTotal.has_value() && nativeTotal->AsInt() == 2, "Native script function call returned wrong total");
    kb::tests::Require(luaTotal.has_value() && luaTotal->AsInt() == 5, "Lua script function call returned wrong total");
    kb::tests::Require(graphTotal.has_value() && graphTotal->AsInt() == 9, "Visual script function call returned wrong total");

    kb::scene::Scene failureScene;
    kb::script::ScriptRuntimeHost failureHost{ failureScene };
    kb::tests::Require(failureHost.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{ .name = "Inventory.FailNoOutput" },
                           .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                               return kb::script::ScriptFunctionCallResult{
                                   .errors = { "inventory failure" },
                               };
                           },
                       }),
        "Script function registry did not register failing no-output function");
    constexpr kb::assets::AssetId kFailingVisualAsset{ 5013U };
    const kb::scene::SceneObject failingGraphObject = failureScene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Failing Visual Function Caller" });
    failureScene.Components().Behaviours().Set(failingGraphObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kFailingVisualAsset.value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    });
    kb::visual::VisualGraphAsset failingGraph{};
    failingGraph.name = "FailingVisualFunctionCaller";
    failingGraph.nodes = {
        kb::visual::VisualGraphNode{ .id = 1U, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick },
        kb::visual::VisualGraphNode{ .id = 2U, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "Function.Inventory.FailNoOutput" },
    };
    failingGraph.pins = {
        kb::visual::VisualGraphPin{ .nodeId = 1U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
    };
    failingGraph.edges = {
        kb::visual::VisualGraphEdge{ .fromNode = 1U, .fromPin = "then", .toNode = 2U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
    };
    const kb::visual::VisualGraphCompileResult failingCompiled = kb::visual::VisualGraphCompiler::Compile(failingGraph);
    kb::tests::Require(failingCompiled.Succeeded(), "Failing visual function caller graph did not compile");
    failureHost.VisualGraphs().Store(kb::visual::VisualGraphRuntimeArtifact{
        .assetId = kFailingVisualAsset,
        .graphName = failingGraph.name,
        .module = failingCompiled.module,
    });
    const kb::script::ScriptRuntimeExecutionResult failingResult = failureHost.Runtime().ExecuteLifecycle(failureScene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(!failingResult.Succeeded(), "Visual script function call swallowed a registry error for a no-output function");
    bool sawFunctionError = false;
    for (const kb::script::ScriptDiagnostic& diagnostic : failingResult.diagnostics) {
        sawFunctionError = sawFunctionError || diagnostic.message.find("inventory failure") != std::string::npos;
    }
    kb::tests::Require(sawFunctionError, "Visual script function call did not surface the registry error diagnostic");
}

void RunScriptRuntimeSceneSystemTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "System Scripted"});
    constexpr kb::assets::AssetId kNativeAsset{1002U};
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    std::vector<std::string> order;
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Created, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("Created");
                       }),
        "Native Created registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Activated, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("Activated");
                       }),
        "Native Activated registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Ready, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("Ready");
                       }),
        "Native Ready registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick, [&order](kb::script::ScriptExecutionContext& context) {
                           if (context.DeltaSeconds() == 0.5F) {
                               order.push_back("Tick");
                           }
                       }),
        "Native Tick registration failed");

    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Native backend registration failed for scene system");
    scene.Runtime().AddSceneSystem(std::make_unique<kb::script::ScriptRuntimeSceneSystem>(runtime));
    static_cast<void>(scene.Runtime().Update(0.5F));

    kb::tests::Require(order.size() == 4U, "Script runtime scene system did not dispatch expected lifecycle events");
    kb::tests::Require(order[0] == "Created" && order[1] == "Activated" && order[2] == "Ready" && order[3] == "Tick",
        "Script runtime scene system dispatched lifecycle events in the wrong order");
}

void RunScriptRuntimeSceneSystemDynamicLifecycleTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kNativeAsset{ 1102U };

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    std::vector<std::string> order;
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Created, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("Created");
                       }),
        "Dynamic lifecycle Created registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Activated, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("Activated");
                       }),
        "Dynamic lifecycle Activated registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Ready, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("Ready");
                       }),
        "Dynamic lifecycle Ready registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("Tick");
                       }),
        "Dynamic lifecycle Tick registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Deactivated, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("Deactivated");
                       }),
        "Dynamic lifecycle Deactivated registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Destroyed, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("Destroyed");
                       }),
        "Dynamic lifecycle Destroyed registration failed");
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Dynamic lifecycle native backend registration failed");

    kb::script::ScriptRuntimeSceneSystem system{ runtime };
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(order.empty(), "Dynamic lifecycle dispatched callbacks before a behaviour existed");

    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Dynamic Scripted" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });
    static_cast<void>(system.ExecuteFrame(scene, 0.0F));
    kb::tests::Require(order.size() == 4U && order[0] == "Created" && order[1] == "Activated" && order[2] == "Ready" && order[3] == "Tick",
        "Dynamic lifecycle did not start a newly attached behaviour before Tick");

    order.clear();
    kb::scene::BehaviourComponent* behaviour = scene.Components().Behaviours().TryGet(object.Entity());
    kb::tests::Require(behaviour != nullptr, "Dynamic lifecycle behaviour was not mutable");
    behaviour->enabled = false;
    scene.Components().Behaviours().MarkModified(object.Entity());
    static_cast<void>(system.ExecuteFrame(scene, 0.0F));
    kb::tests::Require(order.size() == 1U && order[0] == "Deactivated", "Dynamic lifecycle did not deactivate a disabled behaviour");

    order.clear();
    behaviour = scene.Components().Behaviours().TryGet(object.Entity());
    kb::tests::Require(behaviour != nullptr, "Dynamic lifecycle disabled behaviour was not mutable");
    behaviour->enabled = true;
    scene.Components().Behaviours().MarkModified(object.Entity());
    static_cast<void>(system.ExecuteFrame(scene, 0.0F));
    kb::tests::Require(order.size() == 3U && order[0] == "Activated" && order[1] == "Ready" && order[2] == "Tick",
        "Dynamic lifecycle did not reactivate a re-enabled behaviour without recreating it");

    order.clear();
    scene.Components().Behaviours().Remove(object.Entity());
    static_cast<void>(system.ExecuteFrame(scene, 0.0F));
    kb::tests::Require(order.size() == 2U && order[0] == "Deactivated" && order[1] == "Destroyed",
        "Dynamic lifecycle did not shut down a removed behaviour");
}

void RunScriptRuntimeSceneSystemFrameFlowTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kNativeAsset{ 1202U };
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Frame Flow Scripted" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    std::vector<std::string> order;
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::FixedTick, [&order](kb::script::ScriptExecutionContext& context) {
                           order.push_back("FixedTick");
                           context.Emit("FixedTickDone");
                       }),
        "Frame flow FixedTick registration failed");
    kb::tests::Require(native->RegisterEvent(kNativeAsset, "FixedTickDone", [&order](kb::script::ScriptExecutionContext&, const kb::script::ScriptEvent&) {
                           order.push_back("FixedTickDone");
                       }),
        "Frame flow event registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("Tick");
                       }),
        "Frame flow Tick registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::LateTick, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("LateTick");
                       }),
        "Frame flow LateTick registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::BeforeRender, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("BeforeRender");
                       }),
        "Frame flow BeforeRender registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::AfterRender, [&order](kb::script::ScriptExecutionContext&) {
                           order.push_back("AfterRender");
                       }),
        "Frame flow AfterRender registration failed");

    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Frame flow native backend registration failed");
    auto system = std::make_unique<kb::script::ScriptRuntimeSceneSystem>(runtime);
    kb::script::ScriptRuntimeSceneSystem* systemView = system.get();
    scene.Runtime().AddSceneSystem(std::move(system));
    order.clear();

    static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));

    kb::tests::Require(systemView->LastResult().Succeeded(), "Frame flow script runtime produced diagnostics");
    kb::tests::Require(systemView->LastResult().executedBehaviours == 6U, "Frame flow did not execute all lifecycle and event callbacks");
    kb::tests::Require(order.size() == 6U, "Frame flow did not record all callbacks");
    kb::tests::Require(
        order[0] == "FixedTick" && order[1] == "FixedTickDone" && order[2] == "Tick" && order[3] == "LateTick" && order[4] == "BeforeRender" && order[5] == "AfterRender",
        "Frame flow callbacks were not dispatched in the expected order");
}

void RunScriptRuntimeSceneSystemFixedAccumulatorTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kNativeAsset{ 1203U };
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Fixed Accumulator Scripted" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    std::vector<float> fixedDeltas;
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::FixedTick, [&fixedDeltas](kb::script::ScriptExecutionContext& context) {
                           fixedDeltas.push_back(context.DeltaSeconds());
                       }),
        "Fixed accumulator callback registration failed");
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Fixed accumulator native backend registration failed");

    kb::script::ScriptRuntimeSceneSystem system{ runtime };
    const float fixedStep = system.FrameSettings().fixedDeltaSeconds;
    static_cast<void>(system.ExecuteFrame(scene, fixedStep * 0.5F));
    kb::tests::Require(fixedDeltas.empty(), "Fixed accumulator executed before a full fixed step accumulated");
    static_cast<void>(system.ExecuteFrame(scene, fixedStep * 0.5F));
    kb::tests::Require(fixedDeltas.size() == 1U, "Fixed accumulator did not execute after a full fixed step accumulated");
    static_cast<void>(system.ExecuteFrame(scene, fixedStep * 2.0F));
    kb::tests::Require(fixedDeltas.size() == 3U, "Fixed accumulator did not execute multiple fixed steps in one frame");
    kb::tests::Require(kb::tests::NearlyEqual(fixedDeltas[0], fixedStep) && kb::tests::NearlyEqual(fixedDeltas[1], fixedStep) && kb::tests::NearlyEqual(fixedDeltas[2], fixedStep),
        "Fixed accumulator did not pass fixed delta seconds to FixedTick");
}

void RunScriptRuntimeSceneSystemFixedStepSafetyTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kNativeAsset{ 1204U };
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Fixed Safety Scripted" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    std::vector<float> tickDeltas;
    std::size_t fixedTicks = 0U;
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::FixedTick, [&fixedTicks](kb::script::ScriptExecutionContext&) {
                           ++fixedTicks;
                       }),
        "Fixed safety FixedTick callback registration failed");
    kb::tests::Require(native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick, [&tickDeltas](kb::script::ScriptExecutionContext& context) {
                           tickDeltas.push_back(context.DeltaSeconds());
                       }),
        "Fixed safety Tick callback registration failed");
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Fixed safety native backend registration failed");

    kb::script::ScriptRuntimeSceneSystem system{ runtime };
    system.SetFrameSettings(kb::script::ScriptRuntimeFrameSettings{
        .fixedDeltaSeconds = 0.1F,
        .maxFixedStepsPerFrame = 2U,
    });

    static_cast<void>(system.ExecuteFrame(scene, -1.0F));
    kb::tests::Require(fixedTicks == 0U, "Fixed step safety accepted a negative delta for FixedTick");
    kb::tests::Require(!tickDeltas.empty() && kb::tests::NearlyEqual(tickDeltas.back(), 0.0F), "Fixed step safety did not clamp negative Tick delta");

    static_cast<void>(system.ExecuteFrame(scene, 1.0F));
    kb::tests::Require(fixedTicks == 2U, "Fixed step safety did not respect max fixed steps per frame");
    static_cast<void>(system.ExecuteFrame(scene, 0.0F));
    kb::tests::Require(fixedTicks == 2U, "Fixed step safety retained overflow after max fixed steps");

    system.SetFrameSettings(kb::script::ScriptRuntimeFrameSettings{
        .fixedDeltaSeconds = 0.0F,
        .maxFixedStepsPerFrame = 2U,
    });
    static_cast<void>(system.ExecuteFrame(scene, 1.0F));
    kb::tests::Require(fixedTicks == 2U, "Fixed step safety executed FixedTick while fixed step was disabled");
}

void RunVisualGraphScriptBackendDispatchTest() {
    kb::visual::VisualGraphAsset graph{};
    graph.name = "VisualBackendGraph";
    graph.nodes = {
        kb::visual::VisualGraphNode{.id = 1U, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick},
        kb::visual::VisualGraphNode{.id = 2U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "DeltaSeconds"},
        kb::visual::VisualGraphNode{.id = 3U, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "UseDelta"},
    };
    graph.pins = {
        kb::visual::VisualGraphPin{.nodeId = 1U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::Float},
        kb::visual::VisualGraphPin{.nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "delta", .type = kb::visual::VisualGraphValueType::Float},
        kb::visual::VisualGraphPin{.nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{.fromNode = 1U, .fromPin = "then", .toNode = 3U, .toPin = "exec"},
        kb::visual::VisualGraphEdge{.fromNode = 2U, .fromPin = "value", .toNode = 3U, .toPin = "delta", .kind = kb::visual::VisualGraphEdgeKind::Data},
    };

    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Visual graph script backend test graph did not compile");

    constexpr kb::assets::AssetId kVisualAsset{2001U};
    kb::visual::VisualGraphRuntimeRegistry artifacts;
    artifacts.Store(kb::visual::VisualGraphRuntimeArtifact{
        .assetId = kVisualAsset,
        .graphName = graph.name,
        .module = compiled.module,
    });

    float consumedDelta = 0.0F;
    kb::visual::VisualGraphRuntimeBindingRegistry bindings;
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "UseDelta",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "delta", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [&consumedDelta](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               consumedDelta = context.ReadFloat(instruction.inputs[0].sourceNodeId, instruction.inputs[0].sourcePin);
                           },
                       }),
        "Visual graph script backend runtime binding registration failed");

    kb::visual::VisualGraphBehaviourInstanceRegistry instances;
    auto visualBackend = std::make_unique<kb::script::VisualGraphScriptBackend>(artifacts, bindings, instances);

    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Visual Scripted"});
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kVisualAsset.value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    });

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::move(visualBackend)), "Visual graph script backend registration failed");
    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.25F);

    kb::tests::Require(result.Succeeded(), "Visual graph script backend dispatch produced diagnostics");
    kb::tests::Require(result.visitedBehaviours == 1U, "Visual graph script backend did not visit the behaviour");
    kb::tests::Require(result.executedBehaviours == 1U, "Visual graph script backend did not execute through shared script runtime");
    kb::tests::Require(consumedDelta == 0.25F, "Visual graph script backend did not receive shared delta seconds");
}

void RunVisualGraphScriptEventPayloadDispatchTest() {
    kb::visual::VisualGraphAsset graph{};
    graph.name = "VisualGraphPayloadEmitter";
    graph.nodes = {
        kb::visual::VisualGraphNode{ .id = 1U, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick },
        kb::visual::VisualGraphNode{ .id = 2U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "PayloadValue" },
        kb::visual::VisualGraphNode{ .id = 3U, .kind = kb::visual::VisualGraphNodeKind::EmitEvent, .symbol = "GraphPayload" },
    };
    graph.pins = {
        kb::visual::VisualGraphPin{ .nodeId = 1U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "amount", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{ .fromNode = 1U, .fromPin = "then", .toNode = 3U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
        kb::visual::VisualGraphEdge{ .fromNode = 2U, .fromPin = "value", .toNode = 3U, .toPin = "amount", .kind = kb::visual::VisualGraphEdgeKind::Data },
    };

    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Visual graph payload event graph did not compile");

    constexpr kb::assets::AssetId kGraphAsset{ 2102U };
    constexpr kb::assets::AssetId kLuaAsset{ 2103U };
    kb::visual::VisualGraphRuntimeRegistry artifacts;
    artifacts.Store(kb::visual::VisualGraphRuntimeArtifact{
        .assetId = kGraphAsset,
        .graphName = graph.name,
        .module = compiled.module,
    });

    kb::visual::VisualGraphRuntimeBindingRegistry bindings;
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "PayloadValue",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Float } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ 42.5F });
                           },
                       }),
        "Visual graph payload value binding did not register");

    kb::scene::Scene scene;
    const kb::scene::SceneObject graphObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Graph Payload Sender" });
    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lua Payload Receiver" });
    scene.Components().Behaviours().Set(graphObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kGraphAsset.value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    });
    scene.Components().Behaviours().Set(luaObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    FakeLuaRuntime fakeLua;
    fakeLua.emitLifecycleEvent = false;
    kb::visual::VisualGraphBehaviourInstanceRegistry instances;
    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::VisualGraphScriptBackend>(artifacts, bindings, instances)), "Visual graph payload backend registration failed");
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(fakeLua)), "Lua payload backend registration failed");

    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycleAndDispatchEvents(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(result.Succeeded(), "Visual graph payload event dispatch produced diagnostics");
    kb::tests::Require(fakeLua.eventSelf == luaObject.Entity(), "Visual graph payload event was not delivered to Lua backend");
    kb::tests::Require(fakeLua.receivedEventName == "GraphPayload", "Visual graph payload event name was not preserved");
    kb::tests::Require(fakeLua.receivedArgumentCount == 1U, "Visual graph payload event arguments were not preserved");
}

void RunScriptRuntimeAssetPreparerEndToEndTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "Project";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "Player.lua", R"(-- @import Shared.Math
local Math = Import("Shared.Math")
function Tick(self, dt)
    Emit("LuaAssetTicked", { entity = self.entity, delta = dt, sum = Math.add(3, 4) })
end
)");
    WriteTextFile(assetsRoot / "Logic" / "Shared" / "Math.lua", R"(-- @import Offset
local Offset = Import("Offset")
local M = {}
function M.add(a, b)
    return a + b + Offset.value
end
return M
)");
    WriteTextFile(assetsRoot / "Logic" / "Shared" / "Offset.lua", R"(
return { value = 0 }
)");
    WriteTextFile(assetsRoot / "Logic" / "Enemy.kbgraph", R"(kbgraph 1
name EnemyGraph
node 1 Event Tick
pin 1 Output then Void
node 2 EmitEvent GraphAssetTicked
pin 2 Input exec Void
pin 2 Output then Void
edge exec 1 then 2 exec
)");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Script runtime asset preparer project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 4U, "Script runtime asset preparer discovery did not find Lua, nested modules and graph assets");

    const kb::assets::AssetMetadata* luaMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Player.lua");
    const kb::assets::AssetMetadata* graphMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Enemy.kbgraph");
    kb::tests::Require(luaMetadata != nullptr, "Script runtime asset preparer could not find Lua metadata");
    kb::tests::Require(graphMetadata != nullptr, "Script runtime asset preparer could not find graph metadata");

    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lua Asset Object" });
    const kb::scene::SceneObject secondLuaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Second Lua Asset Object" });
    const kb::scene::SceneObject graphObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Graph Asset Object" });
    const std::optional<kb::scene::BehaviourComponent> luaBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*luaMetadata);
    const std::optional<kb::scene::BehaviourComponent> graphBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*graphMetadata);
    kb::tests::Require(luaBehaviour.has_value(), "Script runtime asset preparer could not create Lua behaviour component");
    kb::tests::Require(graphBehaviour.has_value(), "Script runtime asset preparer could not create graph behaviour component");
    scene.Components().Behaviours().Set(luaObject.Entity(), *luaBehaviour);
    scene.Components().Behaviours().Set(secondLuaObject.Entity(), *luaBehaviour);
    scene.Components().Behaviours().Set(graphObject.Entity(), *graphBehaviour);

    kb::script::PucLuaScriptRuntime luaRuntime;
    kb::visual::VisualGraphRuntimeRegistry visualArtifacts;
    kb::script::ScriptRuntimeAssetPreparer preparer{ scene.Assets().Manager(), luaRuntime, visualArtifacts };
    const std::filesystem::path generatedRoot = projectRoot / "Intermediate" / "GeneratedVisualScripts";
    const std::filesystem::path nativeBuildMarker = projectRoot / "Intermediate" / "prepare_native_build.marker";
    preparer.SetVisualGraphSettings(kb::script::ScriptRuntimeVisualGraphPrepareSettings{
        .generatedClassNamespace = "kb::prepared",
        .generatedCodeDirectory = generatedRoot,
        .nativeBuild = kb::visual::VisualGraphNativeBuildDesc{
            .enabled = true,
            .command = std::string{ "cmake -E touch \"" } + nativeBuildMarker.string() + "\"",
            .workingDirectory = projectRoot,
        },
        .writeGeneratedCode = true,
    });
    const kb::script::ScriptRuntimeAssetPrepareResult prepared = preparer.PrepareSceneBehaviours(scene);
    kb::tests::Require(prepared.Succeeded(), "Script runtime asset preparer produced diagnostics");
    kb::tests::Require(prepared.visitedAssets == 2U, "Script runtime asset preparer did not visit unique behaviour assets");
    kb::tests::Require(prepared.preparedAssets == 2U, "Script runtime asset preparer did not prepare unique behaviour assets");
    kb::tests::Require(luaRuntime.HasScript(luaMetadata->id), "Script runtime asset preparer did not load Lua script into VM");
    kb::tests::Require(luaRuntime.HasModule("Shared.Math"), "Script runtime asset preparer did not register declared Lua import module");
    kb::tests::Require(luaRuntime.HasModule("Offset"), "Script runtime asset preparer did not register nested Lua import module");
    kb::tests::Require(visualArtifacts.Contains(graphMetadata->id), "Script runtime asset preparer did not compile graph into runtime registry");
    const kb::visual::VisualGraphRuntimeArtifact* preparedGraph = visualArtifacts.Find(graphMetadata->id);
    kb::tests::Require(preparedGraph != nullptr && std::filesystem::is_regular_file(preparedGraph->generatedFiles.headerPath) &&
            std::filesystem::is_regular_file(preparedGraph->generatedFiles.sourcePath),
        "Script runtime asset preparer did not write generated VisualGraph code");
    kb::tests::Require(std::filesystem::is_regular_file(nativeBuildMarker), "Script runtime asset preparer did not execute VisualGraph native build hook");

    kb::visual::VisualGraphRuntimeBindingRegistry visualBindings;
    kb::visual::VisualGraphBehaviourInstanceRegistry visualInstances;
    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(luaRuntime)), "Script runtime asset preparer Lua backend registration failed");
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::VisualGraphScriptBackend>(visualArtifacts, visualBindings, visualInstances)), "Script runtime asset preparer visual backend registration failed");

    const kb::script::ScriptRuntimeExecutionResult tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.5F);
    kb::tests::Require(tick.Succeeded(), "Prepared script asset runtime Tick produced diagnostics");
    kb::tests::Require(tick.executedBehaviours == 3U, "Prepared script asset runtime did not execute all Lua and graph behaviours");

    bool sawLuaEvent = false;
    bool sawGraphEvent = false;
    bool sawLuaImportValue = false;
    for (const kb::script::ScriptEvent& event : tick.emittedEvents) {
        sawLuaEvent = sawLuaEvent || event.name == "LuaAssetTicked";
        sawGraphEvent = sawGraphEvent || event.name == "GraphAssetTicked";
        if (event.name == "LuaAssetTicked") {
            for (const kb::script::ScriptEventArgument& argument : event.arguments) {
                sawLuaImportValue = sawLuaImportValue || (argument.name == "sum" && argument.value.AsInt() == 7);
            }
        }
    }
    kb::tests::Require(sawLuaEvent, "Prepared script asset runtime did not emit Lua event");
    kb::tests::Require(sawLuaImportValue, "Prepared script asset runtime did not execute declared Lua import module");
    kb::tests::Require(sawGraphEvent, "Prepared script asset runtime did not emit graph event");

    WriteTextFile(assetsRoot / "Logic" / "Enemy.kbgraph", R"(kbgraph 1
name EnemyGraph
node 1 Event Tick
pin 1 Output then Void
node 2 EmitEvent GraphAssetReloaded
pin 2 Input exec Void
pin 2 Output then Void
edge exec 1 then 2 exec
)");
    kb::tests::Require(scene.Assets().Discover() == 4U, "Script runtime asset preparer rediscovery did not refresh changed graph asset");
    const kb::script::ScriptRuntimeAssetPrepareResult graphReloaded = preparer.PrepareSceneBehaviours(scene);
    kb::tests::Require(graphReloaded.Succeeded(), "Script runtime asset preparer did not recompile changed graph asset");
    const kb::script::ScriptRuntimeExecutionResult tickAfterGraphReload = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.5F);
    kb::tests::Require(tickAfterGraphReload.Succeeded(), "Prepared script asset runtime produced diagnostics after graph reload");
    bool sawReloadedGraphEvent = false;
    bool sawOldGraphEvent = false;
    for (const kb::script::ScriptEvent& event : tickAfterGraphReload.emittedEvents) {
        sawReloadedGraphEvent = sawReloadedGraphEvent || event.name == "GraphAssetReloaded";
        sawOldGraphEvent = sawOldGraphEvent || event.name == "GraphAssetTicked";
    }
    kb::tests::Require(sawReloadedGraphEvent && !sawOldGraphEvent, "Script runtime asset preparer did not replace the VisualGraph runtime artifact after file changes");

    WriteTextFile(assetsRoot / "Logic" / "Shared" / "Offset.lua", R"(
return { value = 10 }
)");
    kb::tests::Require(scene.Assets().Discover() == 4U, "Script runtime asset preparer rediscovery did not refresh changed nested Lua module");
    const kb::script::ScriptRuntimeAssetPrepareResult reloaded = preparer.PrepareSceneBehaviours(scene);
    kb::tests::Require(reloaded.Succeeded(), "Script runtime asset preparer did not reload changed Lua module");
    const kb::script::ScriptRuntimeExecutionResult tickAfterModuleReload = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.5F);
    kb::tests::Require(tickAfterModuleReload.Succeeded(), "Prepared script asset runtime produced diagnostics after Lua module reload");
    bool sawReloadedLuaImportValue = false;
    for (const kb::script::ScriptEvent& event : tickAfterModuleReload.emittedEvents) {
        if (event.name != "LuaAssetTicked") {
            continue;
        }
        for (const kb::script::ScriptEventArgument& argument : event.arguments) {
            sawReloadedLuaImportValue = sawReloadedLuaImportValue || (argument.name == "sum" && argument.value.AsInt() == 17);
        }
    }
    kb::tests::Require(sawReloadedLuaImportValue, "Prepared script asset runtime did not reload Lua script when a nested imported module changed");
}

void RunScriptRuntimeSceneSystemAssetPreparationTest() {
    ResetTestRoot();

    kb::script::PucLuaScriptRuntime luaRuntime;
    kb::visual::VisualGraphRuntimeRegistry visualArtifacts;
    kb::visual::VisualGraphRuntimeBindingRegistry visualBindings;
    kb::visual::VisualGraphBehaviourInstanceRegistry visualInstances;
    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(luaRuntime)), "Script scene system Lua backend registration failed");
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::VisualGraphScriptBackend>(visualArtifacts, visualBindings, visualInstances)), "Script scene system visual backend registration failed");

    const std::filesystem::path projectRoot = TestRoot() / "SceneSystemProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "SceneLua.lua", R"(
function Tick(self, dt)
    Emit("SceneSystemLuaTick")
end
)");
    WriteTextFile(assetsRoot / "Logic" / "SceneGraph.kbgraph", R"(kbgraph 1
name SceneGraph
node 1 Event Tick
pin 1 Output then Void
node 2 EmitEvent SceneSystemGraphTick
pin 2 Input exec Void
pin 2 Output then Void
edge exec 1 then 2 exec
)");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Script scene system asset preparation mount failed");
    kb::tests::Require(scene.Assets().Discover() == 2U, "Script scene system asset preparation discovery failed");

    const kb::assets::AssetMetadata* luaMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/SceneLua.lua");
    const kb::assets::AssetMetadata* graphMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/SceneGraph.kbgraph");
    kb::tests::Require(luaMetadata != nullptr && graphMetadata != nullptr, "Script scene system asset metadata was not discovered");

    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Scene Lua" });
    const kb::scene::SceneObject graphObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Scene Graph" });
    const std::optional<kb::scene::BehaviourComponent> luaBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*luaMetadata);
    const std::optional<kb::scene::BehaviourComponent> graphBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*graphMetadata);
    kb::tests::Require(luaBehaviour.has_value() && graphBehaviour.has_value(), "Script scene system could not create behaviour components");
    scene.Components().Behaviours().Set(luaObject.Entity(), *luaBehaviour);
    scene.Components().Behaviours().Set(graphObject.Entity(), *graphBehaviour);

    kb::script::ScriptRuntimeAssetPreparer preparer{ scene.Assets().Manager(), luaRuntime, visualArtifacts };

    auto system = std::make_unique<kb::script::ScriptRuntimeSceneSystem>(runtime, preparer);
    kb::script::ScriptRuntimeSceneSystem* systemView = system.get();
    scene.Runtime().AddSceneSystem(std::move(system));
    kb::tests::Require(systemView->LastPrepareResult().Succeeded(), "Script scene system OnCreate asset preparation failed");
    kb::tests::Require(systemView->LastPrepareResult().preparedAssets == 2U, "Script scene system did not prepare assets on create");

    static_cast<void>(scene.Runtime().Update(0.25F));
    const kb::script::ScriptRuntimeExecutionResult& tick = systemView->LastResult();
    kb::tests::Require(tick.Succeeded(), "Script scene system prepared runtime Tick produced diagnostics");
    kb::tests::Require(tick.executedBehaviours == 2U, "Script scene system did not execute prepared Lua and graph behaviours");

    bool sawLuaEvent = false;
    bool sawGraphEvent = false;
    for (const kb::script::ScriptEvent& event : tick.emittedEvents) {
        sawLuaEvent = sawLuaEvent || event.name == "SceneSystemLuaTick";
        sawGraphEvent = sawGraphEvent || event.name == "SceneSystemGraphTick";
    }
    kb::tests::Require(sawLuaEvent, "Script scene system did not emit Lua Tick event");
    kb::tests::Require(sawGraphEvent, "Script scene system did not emit graph Tick event");
}

void RunScriptRuntimeHostBackendRegistrationTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };

    kb::tests::Require(host.Succeeded(), "Script runtime host produced diagnostics during setup");
    kb::tests::Require(host.Runtime().FindBackend(kb::scene::BehaviourBackend::Native) != nullptr, "Script runtime host did not register Native backend");
    kb::tests::Require(host.Runtime().FindBackend(kb::scene::BehaviourBackend::Lua) != nullptr, "Script runtime host did not register Lua backend");
    kb::tests::Require(host.Runtime().FindBackend(kb::scene::BehaviourBackend::VisualGraph) != nullptr, "Script runtime host did not register VisualGraph backend");
    kb::tests::Require(
        host.VisualGraphRuntimeBindings().Find(kb::visual::VisualGraphIrOpcode::GetComponent, "Self.HasComponent") != nullptr,
        "Script runtime host did not register VisualGraph scene component bindings");
    kb::tests::Require(
        host.VisualGraphNativeBindings().Find(kb::visual::VisualGraphIrOpcode::SetProperty, "Self.SetProperty.Bool") != nullptr,
        "Script runtime host did not register VisualGraph native scene component bindings");
    kb::tests::Require(
        host.VisualGraphRuntimeBindings().Find(kb::visual::VisualGraphIrOpcode::SetProperty, "Shared.Set.Int") != nullptr,
        "Script runtime host did not register VisualGraph shared state runtime bindings");
    kb::tests::Require(
        host.VisualGraphNativeBindings().Find(kb::visual::VisualGraphIrOpcode::SetProperty, "Shared.Set.Int") != nullptr,
        "Script runtime host did not register VisualGraph shared state native bindings");
    const kb::visual::VisualGraphNodeCatalog catalog = host.CreateVisualGraphNodeCatalog();
    kb::tests::Require(
        catalog.Find("NativeBinding:SetProperty:Self.SetProperty.Bool") != nullptr,
        "Script runtime host visual graph catalog did not expose scene property bindings");
    kb::tests::Require(
        catalog.Find("NativeBinding:SetProperty:Shared.Set.Int") != nullptr,
        "Script runtime host visual graph catalog did not expose shared state bindings");
}

void RunScriptRuntimeHostSceneSystemTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "HostProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "HostLua.lua", R"(
function Tick(self, dt)
    self:SetProperty("Transform", "localPosition.x", 2.75)
end
)");
    WriteTextFile(assetsRoot / "Logic" / "HostGraph.kbgraph", R"(kbgraph 1
name HostGraph
node 1 Event Tick
pin 1 Output then Void
node 2 GetProperty VisibilityComponentName
pin 2 Output value String
node 3 GetProperty VisibilityPropertyName
pin 3 Output value String
node 4 GetProperty VisibilityValue
pin 4 Output value Bool
node 5 SetProperty Self.SetProperty.Bool
pin 5 Input exec Void
pin 5 Input component String
pin 5 Input property String
pin 5 Input value Bool
pin 5 Output then Void
pin 5 Output succeeded Bool
edge exec 1 then 5 exec
edge data 2 value 5 component
edge data 3 value 5 property
edge data 4 value 5 value
)");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Script runtime host project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 2U, "Script runtime host asset discovery failed");

    const kb::assets::AssetMetadata* luaMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/HostLua.lua");
    const kb::assets::AssetMetadata* graphMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/HostGraph.kbgraph");
    kb::tests::Require(luaMetadata != nullptr && graphMetadata != nullptr, "Script runtime host asset metadata was not discovered");

    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script runtime host setup failed");
    const kb::visual::VisualGraphNodeCatalog hostNodeCatalog = host.CreateVisualGraphNodeCatalog();
    std::size_t selfSetPropertyBoolEntries = 0U;
    kb::visual::VisualGraphNodeCatalogSource selfSetPropertyBoolSource = kb::visual::VisualGraphNodeCatalogSource::BuiltIn;
    for (const kb::visual::VisualGraphNodeCatalogEntry& entry : hostNodeCatalog.Entries()) {
        if (entry.kind == kb::visual::VisualGraphNodeKind::SetProperty && entry.symbol == "Self.SetProperty.Bool") {
            ++selfSetPropertyBoolEntries;
            selfSetPropertyBoolSource = entry.source;
        }
    }
    kb::tests::Require(selfSetPropertyBoolEntries == 1U, "Script runtime host node catalog duplicated scene component bindings");
    kb::tests::Require(selfSetPropertyBoolSource == kb::visual::VisualGraphNodeCatalogSource::NativeBinding, "Script runtime host node catalog did not prefer native scene component bindings");
    kb::tests::Require(host.VisualGraphRuntimeBindings().Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "VisibilityComponentName",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::String } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ std::string{ "Visibility" } });
                           },
                       }),
        "Script runtime host test visibility component binding did not register");
    kb::tests::Require(host.VisualGraphRuntimeBindings().Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "VisibilityPropertyName",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::String } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ std::string{ "visible" } });
                           },
                       }),
        "Script runtime host test visibility property binding did not register");
    kb::tests::Require(host.VisualGraphRuntimeBindings().Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "VisibilityValue",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Bool } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ false });
                           },
                       }),
        "Script runtime host test visibility value binding did not register");

    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Host Lua" });
    const kb::scene::SceneObject graphObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Host Graph" });
    const std::optional<kb::scene::BehaviourComponent> luaBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*luaMetadata);
    const std::optional<kb::scene::BehaviourComponent> graphBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*graphMetadata);
    kb::tests::Require(luaBehaviour.has_value() && graphBehaviour.has_value(), "Script runtime host could not create behaviour components");
    scene.Components().Behaviours().Set(luaObject.Entity(), *luaBehaviour);
    scene.Components().Behaviours().Set(graphObject.Entity(), *graphBehaviour);

    kb::tests::Require(host.InstallSceneSystem(), "Script runtime host did not install scene system");
    static_cast<void>(scene.Runtime().Update(0.125F));

    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(luaObject.Entity()).localPosition.x, 2.75F), "Script runtime host did not execute Lua behaviour");
    kb::tests::Require(!scene.Components().Visibility().Get(graphObject.Entity()).visible, "Script runtime host did not execute VisualGraph behaviour");
    kb::tests::Require(host.LuaRuntime().HasScript(luaMetadata->id), "Script runtime host did not prepare Lua asset");
    kb::tests::Require(host.VisualGraphs().Contains(graphMetadata->id), "Script runtime host did not prepare VisualGraph asset");
    const kb::visual::VisualGraphRuntimeArtifact* hostGraphArtifact = host.VisualGraphs().Find(graphMetadata->id);
    kb::tests::Require(hostGraphArtifact != nullptr, "Script runtime host did not retain VisualGraph artifact");
    kb::tests::Require(
        hostGraphArtifact->nativeCode.source.find("context.StoreBool(5U, \"succeeded\", context.SelfSetPropertyBool(context.ReadString(2U, \"value\"), context.ReadString(3U, \"value\"), context.ReadBool(4U, \"value\")));") != std::string::npos,
        "Script runtime host did not generate direct native scene component binding");
    kb::tests::Require(
        hostGraphArtifact->nativeCode.source.find("context.Trace(\"SetProperty\", \"Self.SetProperty.Bool\")") == std::string::npos,
        "Script runtime host used fallback codegen for a mapped scene component binding");
}

void RunScriptRuntimeHostNativeDescriptorBindingTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "HostNativeProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    const std::filesystem::path pluginPath = KB_NATIVE_SCRIPT_TEST_PLUGIN_PATH;
    const std::filesystem::path buildMarker = projectRoot / "native_descriptor_build.marker";
    kb::tests::Require(!pluginPath.empty() && std::filesystem::is_regular_file(pluginPath), "Script runtime host native test plugin DLL is missing");
    WriteTextFile(assetsRoot / "Logic" / "HostNative.native",
        std::string{ "name Host Native\nsymbol tests.NativePlugin\nmodule = " } + pluginPath.string() +
            "\nentry = kb_register_native_scripts\nbuild = cmake -E touch \"" + buildMarker.string() + "\"\n");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Script runtime host native project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Script runtime host native asset discovery failed");

    const kb::assets::AssetMetadata* nativeMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/HostNative.native");
    kb::tests::Require(nativeMetadata != nullptr, "Script runtime host native metadata was not discovered");

    kb::script::ScriptRuntimeHost host{
        scene,
        kb::script::ScriptRuntimeHostOptions{
            .nativePrepareSettings = kb::script::ScriptRuntimeNativePrepareSettings{
                .shadowCopyDirectory = projectRoot / "NativePluginShadow",
                .buildPlugins = true,
                .loadPlugins = true,
            },
        },
    };
    kb::tests::Require(host.Succeeded(), "Script runtime host native setup failed");

    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Host Native" });
    const std::optional<kb::scene::BehaviourComponent> nativeBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*nativeMetadata);
    kb::tests::Require(nativeBehaviour.has_value(), "Script runtime host could not create native behaviour component");
    scene.Components().Behaviours().Set(object.Entity(), *nativeBehaviour);

    kb::tests::Require(host.InstallSceneSystem(), "Script runtime host did not install native scene system");
    static_cast<void>(scene.Runtime().Update(0.016F));

    const std::optional<kb::script::ScriptValue> tickValue = host.SharedState().Get("nativePlugin.Tick");
    kb::tests::Require(tickValue.has_value() && tickValue->AsInt() == 1, "Script runtime host did not load native descriptor plugin symbol");
    kb::tests::Require(std::filesystem::is_regular_file(buildMarker), "Script runtime host did not execute native descriptor build command");

    const std::filesystem::path rebuildMarker = projectRoot / "native_descriptor_rebuild.marker";
    WriteTextFile(assetsRoot / "Logic" / "HostNative.native",
        std::string{ "name Host Native\nsymbol tests.NativePlugin\nmodule = " } + pluginPath.string() +
            "\nentry = kb_register_native_scripts\nbuild = cmake -E touch \"" + rebuildMarker.string() + "\"\n");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Script runtime host native asset rediscovery failed");
    const kb::script::ScriptRuntimeAssetPrepareResult nativeReprepared = host.AssetPreparer().PrepareSceneBehaviours(scene);
    kb::tests::Require(nativeReprepared.Succeeded(), "Script runtime host native descriptor did not reprepare after file changes");
    kb::tests::Require(std::filesystem::is_regular_file(rebuildMarker), "Script runtime asset preparer did not rerun native build after descriptor content changed");
}

void RunScriptRuntimeHostFrameSettingsTest() {
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kNativeAsset{ 2301U };
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Host Fixed Settings" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kNativeAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    kb::script::ScriptRuntimeHost host{
        scene,
        kb::script::ScriptRuntimeHostOptions{
            .frameSettings = kb::script::ScriptRuntimeFrameSettings{
                .fixedDeltaSeconds = 0.1F,
                .maxFixedStepsPerFrame = 4U,
            },
        },
    };
    std::vector<float> fixedDeltas;
    kb::tests::Require(host.NativeBackend().RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::FixedTick, [&fixedDeltas](kb::script::ScriptExecutionContext& context) {
                           fixedDeltas.push_back(context.DeltaSeconds());
                       }),
        "Script runtime host fixed settings callback registration failed");
    kb::tests::Require(host.InstallSceneSystem(), "Script runtime host fixed settings scene system install failed");

    static_cast<void>(scene.Runtime().Update(0.05F));
    kb::tests::Require(fixedDeltas.empty(), "Script runtime host ignored custom fixed step accumulator");
    static_cast<void>(scene.Runtime().Update(0.05F));
    kb::tests::Require(fixedDeltas.size() == 1U && kb::tests::NearlyEqual(fixedDeltas[0], 0.1F), "Script runtime host did not pass custom fixed step to scene system");
}

void RunScriptSceneComponentApiTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Script Api Object" });
    scene.Components().Cameras().Set(object.Entity(), kb::scene::CameraComponent{});

    kb::tests::Require(kb::script::ScriptSceneComponentApi::HasComponent(scene, object.Entity(), "Transform"), "Script component API did not see Transform");
    kb::tests::Require(kb::script::ScriptSceneComponentApi::HasComponent(scene, object.Entity(), "Visibility"), "Script component API did not see Visibility");
    kb::tests::Require(kb::script::ScriptSceneComponentApi::HasComponent(scene, object.Entity(), "Camera"), "Script component API did not see Camera");
    kb::tests::Require(!kb::script::ScriptSceneComponentApi::HasComponent(scene, object.Entity(), "Light"), "Script component API reported missing Light as present");
    const std::span<const kb::script::ScriptSceneComponentPropertyDesc> transformProperties = kb::script::ScriptSceneComponentApi::ComponentProperties("Transform");
    kb::tests::Require(transformProperties.size() == 13U, "Script component API did not expose Transform property reflection");
    kb::tests::Require(transformProperties[0].name == "localPosition.x" && transformProperties[0].type == kb::script::ScriptValueType::Float && transformProperties[0].writable,
        "Script component API exposed invalid Transform.localPosition.x metadata");
    kb::tests::Require(transformProperties[10].name == "worldPosition.x" && !transformProperties[10].writable,
        "Script component API did not mark Transform.worldPosition as read-only");

    const kb::script::ScriptSceneComponentMutationResult setX = kb::script::ScriptSceneComponentApi::SetProperty(
        scene,
        object.Entity(),
        "Transform",
        "localPosition.x",
        kb::script::ScriptValue{ 12.5F });
    kb::tests::Require(setX.succeeded, "Script component API did not set Transform.localPosition.x");
    const kb::script::ScriptSceneComponentPropertyResult getX = kb::script::ScriptSceneComponentApi::GetProperty(
        scene,
        object.Entity(),
        "Transform",
        "localPosition.x");
    kb::tests::Require(getX.succeeded && kb::tests::NearlyEqual(getX.value.AsFloat(), 12.5F), "Script component API did not read Transform.localPosition.x");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(object.Entity()).localPosition.x, 12.5F), "Script component API did not mutate Transform storage");
    const kb::script::ScriptSceneComponentMutationResult setWorldX = kb::script::ScriptSceneComponentApi::SetProperty(
        scene,
        object.Entity(),
        "Transform",
        "worldPosition.x",
        kb::script::ScriptValue{ 3.0F });
    kb::tests::Require(!setWorldX.succeeded, "Script component API accepted write to read-only Transform.worldPosition.x");

    const kb::script::ScriptSceneComponentMutationResult setVisible = kb::script::ScriptSceneComponentApi::SetProperty(
        scene,
        object.Entity(),
        "Visibility",
        "visible",
        kb::script::ScriptValue{ false });
    kb::tests::Require(setVisible.succeeded, "Script component API did not set Visibility.visible");
    const kb::script::ScriptSceneComponentPropertyResult getVisible = kb::script::ScriptSceneComponentApi::GetProperty(
        scene,
        object.Entity(),
        "Visibility",
        "visible");
    kb::tests::Require(getVisible.succeeded && !getVisible.value.AsBool(true), "Script component API did not read Visibility.visible");

    const kb::script::ScriptSceneComponentMutationResult setFov = kb::script::ScriptSceneComponentApi::SetProperty(
        scene,
        object.Entity(),
        "Camera",
        "verticalFovDegrees",
        kb::script::ScriptValue{ 80 });
    kb::tests::Require(setFov.succeeded, "Script component API did not accept int-to-float camera write");
    const kb::script::ScriptSceneComponentPropertyResult getFov = kb::script::ScriptSceneComponentApi::GetProperty(
        scene,
        object.Entity(),
        "Camera",
        "verticalFovDegrees");
    kb::tests::Require(getFov.succeeded && kb::tests::NearlyEqual(getFov.value.AsFloat(), 80.0F), "Script component API did not read Camera.verticalFovDegrees");

    const kb::script::ScriptSceneComponentMutationResult badType = kb::script::ScriptSceneComponentApi::SetProperty(
        scene,
        object.Entity(),
        "Visibility",
        "visible",
        kb::script::ScriptValue{ 1 });
    kb::tests::Require(!badType.succeeded && !badType.error.empty(), "Script component API accepted wrong value type");

    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = 999U,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    const kb::script::ScriptSceneComponentMutationResult setTickGroup = kb::script::ScriptSceneComponentApi::SetProperty(
        scene,
        object.Entity(),
        "Behaviour",
        "tickGroup",
        kb::script::ScriptValue{ static_cast<int>(kb::scene::BehaviourTickGroup::Camera) });
    kb::tests::Require(setTickGroup.succeeded, "Script component API did not set Behaviour.tickGroup");
    const kb::script::ScriptSceneComponentMutationResult setExecutionOrder = kb::script::ScriptSceneComponentApi::SetProperty(
        scene,
        object.Entity(),
        "Behaviour",
        "executionOrder",
        kb::script::ScriptValue{ -25 });
    kb::tests::Require(setExecutionOrder.succeeded, "Script component API did not set Behaviour.executionOrder");
    const kb::scene::BehaviourComponent* behaviour = scene.Components().Behaviours().TryGet(object.Entity());
    kb::tests::Require(
        behaviour != nullptr && behaviour->tickGroup == kb::scene::BehaviourTickGroup::Camera && behaviour->executionOrder == -25,
        "Script component API did not mutate Behaviour scheduling fields");
    const kb::script::ScriptSceneComponentMutationResult badTickGroup = kb::script::ScriptSceneComponentApi::SetProperty(
        scene,
        object.Entity(),
        "Behaviour",
        "tickGroup",
        kb::script::ScriptValue{ 99 });
    kb::tests::Require(!badTickGroup.succeeded, "Script component API accepted invalid Behaviour.tickGroup");
}

void RunVisualGraphSceneComponentBindingTest() {
    kb::visual::VisualGraphAsset graph{};
    graph.name = "VisualSceneComponentBinding";
    graph.nodes = {
        kb::visual::VisualGraphNode{ .id = 1U, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick },
        kb::visual::VisualGraphNode{ .id = 2U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "ComponentName" },
        kb::visual::VisualGraphNode{ .id = 3U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "PropertyName" },
        kb::visual::VisualGraphNode{ .id = 4U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "NewValue" },
        kb::visual::VisualGraphNode{ .id = 5U, .kind = kb::visual::VisualGraphNodeKind::SetProperty, .symbol = "Self.SetProperty" },
        kb::visual::VisualGraphNode{ .id = 6U, .kind = kb::visual::VisualGraphNodeKind::EmitEvent, .symbol = "GraphSetTransform" },
        kb::visual::VisualGraphNode{ .id = 7U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "VisibilityComponentName" },
        kb::visual::VisualGraphNode{ .id = 8U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "VisibilityPropertyName" },
        kb::visual::VisualGraphNode{ .id = 9U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "VisibilityValue" },
        kb::visual::VisualGraphNode{ .id = 10U, .kind = kb::visual::VisualGraphNodeKind::SetProperty, .symbol = "Self.SetProperty.Bool" },
    };
    graph.pins = {
        kb::visual::VisualGraphPin{ .nodeId = 1U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "component", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "property", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "value", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool },
        kb::visual::VisualGraphPin{ .nodeId = 6U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 7U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 8U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 9U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::Bool },
        kb::visual::VisualGraphPin{ .nodeId = 10U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 10U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "component", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 10U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "property", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 10U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "value", .type = kb::visual::VisualGraphValueType::Bool },
        kb::visual::VisualGraphPin{ .nodeId = 10U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 10U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool },
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{ .fromNode = 1U, .fromPin = "then", .toNode = 5U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
        kb::visual::VisualGraphEdge{ .fromNode = 5U, .fromPin = "then", .toNode = 10U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
        kb::visual::VisualGraphEdge{ .fromNode = 10U, .fromPin = "then", .toNode = 6U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
        kb::visual::VisualGraphEdge{ .fromNode = 2U, .fromPin = "value", .toNode = 5U, .toPin = "component", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 3U, .fromPin = "value", .toNode = 5U, .toPin = "property", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 4U, .fromPin = "value", .toNode = 5U, .toPin = "value", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 7U, .fromPin = "value", .toNode = 10U, .toPin = "component", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 8U, .fromPin = "value", .toNode = 10U, .toPin = "property", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 9U, .fromPin = "value", .toNode = 10U, .toPin = "value", .kind = kb::visual::VisualGraphEdgeKind::Data },
    };

    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Visual graph scene component binding graph did not compile");

    constexpr kb::assets::AssetId kVisualAsset{ 2201U };
    kb::visual::VisualGraphRuntimeRegistry artifacts;
    artifacts.Store(kb::visual::VisualGraphRuntimeArtifact{
        .assetId = kVisualAsset,
        .graphName = graph.name,
        .module = compiled.module,
    });

    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Visual Component Binding" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kVisualAsset.value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    });

    kb::visual::VisualGraphRuntimeBindingRegistry bindings;
    kb::tests::Require(kb::script::ScriptSceneVisualGraphBindings::Register(bindings, scene), "Visual graph script scene bindings did not register");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "ComponentName",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::String } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ std::string{ "Transform" } });
                           },
                       }),
        "Visual graph test component name binding did not register");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "PropertyName",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::String } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ std::string{ "localPosition.x" } });
                           },
                       }),
        "Visual graph test property name binding did not register");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "NewValue",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Float } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ 9.25F });
                           },
                       }),
        "Visual graph test value binding did not register");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "VisibilityComponentName",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::String } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ std::string{ "Visibility" } });
                           },
                       }),
        "Visual graph test visibility component name binding did not register");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "VisibilityPropertyName",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::String } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ std::string{ "visible" } });
                           },
                       }),
        "Visual graph test visibility property name binding did not register");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "VisibilityValue",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Bool } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ false });
                           },
                       }),
        "Visual graph test visibility value binding did not register");

    kb::visual::VisualGraphBehaviourInstanceRegistry instances;
    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::VisualGraphScriptBackend>(artifacts, bindings, instances)), "Visual graph scene component backend registration failed");

    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(result.Succeeded(), "Visual graph scene component binding execution produced diagnostics");
    kb::tests::Require(result.executedBehaviours == 1U, "Visual graph scene component binding did not execute");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(object.Entity()).localPosition.x, 9.25F), "Visual graph scene component binding did not mutate Transform");
    kb::tests::Require(!scene.Components().Visibility().Get(object.Entity()).visible, "Visual graph scene component binding did not mutate Visibility.visible");
    kb::tests::Require(result.emittedEvents.size() == 1U && result.emittedEvents[0].name == "GraphSetTransform", "Visual graph scene component binding did not continue execution");
}

} // namespace

namespace kb::tests {

void RunScriptRuntimeTests() {
    RunNativeScriptRuntimeDispatchTest();
    RunNativeScriptPluginManagerDispatchTest();
    RunNativeScriptBuildPipelineTest();
    RunScriptRuntimeExecutionOrderTest();
    RunLuaScriptRuntimeDispatchTest();
    RunPucLuaScriptRuntimeDispatchTest();
    RunPucLuaScriptRuntimeModulesReloadAndDiagnosticsTest();
    RunLuaExposedVariablesRuntimeTest();
    RunCrossBackendEventDispatchTest();
    RunTargetedEventDispatchTest();
    RunCrossBackendSharedStateTest();
    RunScriptFunctionRegistryCrossBackendTest();
    RunScriptRuntimeSceneSystemTest();
    RunScriptRuntimeSceneSystemDynamicLifecycleTest();
    RunScriptRuntimeSceneSystemFrameFlowTest();
    RunScriptRuntimeSceneSystemFixedAccumulatorTest();
    RunScriptRuntimeSceneSystemFixedStepSafetyTest();
    RunVisualGraphScriptBackendDispatchTest();
    RunVisualGraphScriptEventPayloadDispatchTest();
    RunScriptRuntimeAssetPreparerEndToEndTest();
    RunScriptRuntimeSceneSystemAssetPreparationTest();
    RunScriptRuntimeHostBackendRegistrationTest();
    RunScriptRuntimeHostSceneSystemTest();
    RunScriptRuntimeHostNativeDescriptorBindingTest();
    RunScriptRuntimeHostFrameSettingsTest();
    RunScriptSceneComponentApiTest();
    RunVisualGraphSceneComponentBindingTest();
}

} // namespace kb::tests
