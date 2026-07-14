#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/audio/AudioPlayback.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/input/InputActionAsset.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/input/InputMappingContextAsset.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneLoadedContent.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTimers.hpp"
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
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

#ifndef KB_NATIVE_SCRIPT_TEST_PLUGIN_PATH
#define KB_NATIVE_SCRIPT_TEST_PLUGIN_PATH ""
#endif

#ifndef __has_feature
#define __has_feature(feature) 0
#endif

// Apple ASan can crash inside dlopen while registering globals for repeated shadow copies of the same test dylib.
#if defined(__APPLE__) && (__has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__))
#define KB_SKIP_NATIVE_SCRIPT_SHADOW_COPY_FAILURE_LOAD_TEST 1
#else
#define KB_SKIP_NATIVE_SCRIPT_SHADOW_COPY_FAILURE_LOAD_TEST 0
#endif

class ProbeAudioPlaybackBackend final : public kb::audio::IAudioPlaybackBackend {
public:
    [[nodiscard]] kb::audio::AudioPlayResult PlayOneShot(kb::scene::Scene& scene, const kb::audio::AudioPlayDesc& desc) override {
        static_cast<void>(scene);
        played.push_back(desc);
        return kb::audio::AudioPlayResult{ .started = true, .voiceId = nextVoiceId++, .error = {} };
    }

    void StopAll(kb::scene::Scene& scene) noexcept override {
        static_cast<void>(scene);
        ++stopAllCount;
    }

    std::vector<kb::audio::AudioPlayDesc> played;
    std::uint64_t nextVoiceId = 1U;
    int stopAllCount = 0;
};

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

#if !KB_SKIP_NATIVE_SCRIPT_SHADOW_COPY_FAILURE_LOAD_TEST
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
#endif
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

// LIB-012: a queued/pending event command (the `pending` vector inside
// ScriptRuntime::DrainEvents) targeting an entity that gets destroyed
// before that event's dispatch turn must be cancelled — never delivered to
// a dangling/stale entity, and never a crash or diagnostic-of-doom.
// DispatchEvent re-collects the live behaviour set fresh on every dispatch
// turn (ScriptRuntime.cpp: DispatchSceneBehaviours), so a target destroyed
// earlier in the same frame is simply absent from that later snapshot;
// this test proves the resulting silent drop is the actual, safe behavior.
void RunPendingCommandCancelledByDestroyTest() {
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kSenderAsset{ 5601U };
    constexpr kb::assets::AssetId kTargetAsset{ 5602U };

    const kb::scene::SceneObject senderObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "PendingCommandSender" });
    const kb::scene::SceneObject targetObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "PendingCommandTarget" });
    scene.Components().Behaviours().Set(senderObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kSenderAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
        .executionOrder = 0,
    });
    scene.Components().Behaviours().Set(targetObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kTargetAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
        .executionOrder = 1,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();

    const kb::scene::SceneEntity target = targetObject.Entity();
    kb::tests::Require(
        native->RegisterLifecycle(kSenderAsset, kb::script::ScriptLifecycleEvent::Tick, [target](kb::script::ScriptExecutionContext& context) {
            context.EmitTo(target, "PendingCommand", {});
            context.GetScene().Entities().Destroy(target);
        }),
        "Pending command cancellation sender registration failed");

    int deliveries = 0;
    kb::tests::Require(
        native->RegisterEvent(kTargetAsset, "PendingCommand", [&deliveries](kb::script::ScriptExecutionContext&, const kb::script::ScriptEvent&) {
            ++deliveries;
        }),
        "Pending command cancellation target registration failed");

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Pending command cancellation backend registration failed");

    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycleAndDispatchEvents(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(result.Succeeded(), "Pending command cancellation produced diagnostics");
    kb::tests::Require(!scene.Entities().IsAlive(target), "Pending command cancellation test fixture must have destroyed the target");
    kb::tests::Require(deliveries == 0, "A pending command targeting an entity destroyed before its dispatch turn must be cancelled, not delivered");
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

// LIB-061: a CallNative node whose "failed" exec output IS wired must run
// the wired handler instead of the whole Tick halting at the point of
// failure — the "idiomatic Visual Graph adapter" for a fallible function
// call, mirroring the Branch node's "true"/"false" pair. The overall Tick
// still reports Succeeded() == false (a real failure genuinely happened,
// and ScriptDiagnostic carries no severity to distinguish "handled" from
// "fatal" — see the design note in VisualGraphRuntimeExecutor::ExecuteNode)
// but, unlike an unwired failure, the success branch must NOT also run.
void RunVisualGraphCallNativeFailureBranchTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "CallNative failure branch host setup failed");

    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{ .name = "Tests.AlwaysFails" },
                           .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                               return kb::script::ScriptFunctionCallResult{ .errors = { "deliberate LIB-061 test failure" } };
                           },
                       }),
        "CallNative failure branch did not register Tests.AlwaysFails");

    bool successPathRan = false;
    bool failurePathRan = false;
    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{ .name = "Tests.MarkSuccessPath" },
                           .callback = [&successPathRan](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                               successPathRan = true;
                               return kb::script::ScriptFunctionCallResult{ .executed = true };
                           },
                       }),
        "CallNative failure branch did not register Tests.MarkSuccessPath");
    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{ .name = "Tests.MarkFailurePath" },
                           .callback = [&failurePathRan](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                               failurePathRan = true;
                               return kb::script::ScriptFunctionCallResult{ .executed = true };
                           },
                       }),
        "CallNative failure branch did not register Tests.MarkFailurePath");

    constexpr kb::assets::AssetId kAsset{ 5015U };
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "CallNativeFailureBranch" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kAsset.value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    });

    kb::visual::VisualGraphAsset graph{};
    graph.name = "CallNativeFailureBranch";
    graph.nodes = {
        kb::visual::VisualGraphNode{ .id = 1U, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick },
        kb::visual::VisualGraphNode{ .id = 2U, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "Function.Tests.AlwaysFails" },
        kb::visual::VisualGraphNode{ .id = 3U, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "Function.Tests.MarkSuccessPath" },
        kb::visual::VisualGraphNode{ .id = 4U, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "Function.Tests.MarkFailurePath" },
    };
    graph.pins = {
        kb::visual::VisualGraphPin{ .nodeId = 1U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "failed", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{ .fromNode = 1U, .fromPin = "then", .toNode = 2U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
        kb::visual::VisualGraphEdge{ .fromNode = 2U, .fromPin = "then", .toNode = 3U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
        kb::visual::VisualGraphEdge{ .fromNode = 2U, .fromPin = "failed", .toNode = 4U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
    };
    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "CallNative failure branch graph did not compile");

    host.VisualGraphs().Store(kb::visual::VisualGraphRuntimeArtifact{
        .assetId = kAsset,
        .graphName = graph.name,
        .module = compiled.module,
    });

    const kb::script::ScriptRuntimeExecutionResult result = host.Runtime().ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(!result.Succeeded(), "LIB-061: a genuinely failed function call must still report Succeeded() == false, handled or not");
    kb::tests::Require(!successPathRan, "LIB-061: the \"then\" (success) branch must NOT run after the call failed");
    kb::tests::Require(failurePathRan, "LIB-061: a wired \"failed\" exec output must run its handler instead of the Tick simply halting");
    bool sawFailureMessage = false;
    for (const kb::script::ScriptDiagnostic& diagnostic : result.diagnostics) {
        sawFailureMessage = sawFailureMessage || diagnostic.message.find("deliberate LIB-061 test failure") != std::string::npos;
    }
    kb::tests::Require(sawFailureMessage, "LIB-061: a handled call failure must still be recorded as a diagnostic, not silently dropped");
}

// LIB-061: formalizes and tests, from REAL executed Lua script text (not
// just the C++ marshalling code in isolation), the idiomatic Lua Result
// adapter this codebase already implements for a fallible CallFunction:
// `value, err = CallFunction(...)` — nil plus an error string on failure,
// the plain value (or a table, for multi-output) on success. This is
// Lua's own idiom for "Result<T,E>" (Lua has no Option/Result type, but
// this dual-return-plus-nil-check pattern is the idiomatic equivalent —
// see PucLuaFunctionApi.cpp::LuaCallFunction), exercised end-to-end here
// for the first time.
void RunLuaCallFunctionResultAdapterTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Lua Result adapter host setup failed");

    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{ .name = "Tests.AlwaysFailsForLua" },
                           .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                               return kb::script::ScriptFunctionCallResult{ .errors = { "lua adapter test failure" } };
                           },
                       }),
        "Lua Result adapter did not register Tests.AlwaysFailsForLua");
    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{
                               .name = "Tests.SucceedsForLua",
                               .outputs = { kb::script::ScriptFunctionPin{ .name = "value", .type = kb::script::ScriptValueType::Int } },
                           },
                           .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                               return kb::script::ScriptFunctionCallResult{
                                   .executed = true,
                                   .outputs = { kb::script::ScriptFunctionArgument{ .name = "value", .value = kb::script::ScriptValue{ 42 } } },
                               };
                           },
                       }),
        "Lua Result adapter did not register Tests.SucceedsForLua");

    constexpr kb::assets::AssetId kLuaAsset{ 5016U };
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "LuaResultAdapter" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    const kb::script::PucLuaLoadResult loaded = host.LuaRuntime().LoadScript(kLuaAsset, R"(
function Tick(self, dt)
    local failedValue, failedError = CallFunction("Tests.AlwaysFailsForLua", {})
    SetShared("luaSawNilOnFailure", failedValue == nil)
    SetShared("luaSawErrorString", type(failedError) == "string")
    SetShared("luaErrorMessage", failedError)

    local succeededValue, succeededError = CallFunction("Tests.SucceedsForLua", {})
    SetShared("luaSawValueOnSuccess", succeededValue)
    SetShared("luaSawNilErrorOnSuccess", succeededError == nil)
end
)");
    kb::tests::Require(loaded.succeeded, "Lua Result adapter script did not load");

    const kb::script::ScriptRuntimeExecutionResult result = host.Runtime().ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(result.Succeeded(), "Lua Result adapter Tick produced diagnostics");

    const std::optional<kb::script::ScriptValue> sawNilOnFailure = host.SharedState().Get("luaSawNilOnFailure");
    kb::tests::Require(sawNilOnFailure.has_value() && sawNilOnFailure->AsBool(), "Lua CallFunction must return nil as its first value when the call fails");
    const std::optional<kb::script::ScriptValue> sawErrorString = host.SharedState().Get("luaSawErrorString");
    kb::tests::Require(sawErrorString.has_value() && sawErrorString->AsBool(), "Lua CallFunction must return a string as its second value when the call fails");
    const std::optional<kb::script::ScriptValue> errorMessage = host.SharedState().Get("luaErrorMessage");
    kb::tests::Require(errorMessage.has_value() && errorMessage->AsString() == "lua adapter test failure", "Lua CallFunction's error string must be the real registry error message, not a generic placeholder");

    const std::optional<kb::script::ScriptValue> sawValueOnSuccess = host.SharedState().Get("luaSawValueOnSuccess");
    kb::tests::Require(sawValueOnSuccess.has_value() && sawValueOnSuccess->AsInt() == 42, "Lua CallFunction must return the real value as its first result when the call succeeds");
    const std::optional<kb::script::ScriptValue> sawNilErrorOnSuccess = host.SharedState().Get("luaSawNilErrorOnSuccess");
    kb::tests::Require(sawNilErrorOnSuccess.has_value() && sawNilErrorOnSuccess->AsBool(), "Lua CallFunction must return nil as its second value when the call succeeds, so `if err then` idiomatically detects failure only");
}

// LIB-010: a registered callback throwing a C++ exception must become a
// ScriptFunctionCallResult error, not propagate. This is the single choke
// point every caller (Native direct call, Lua's CallFunction, the future
// Visual Graph CallNative node) goes through, so this also protects the Lua
// boundary, where an uncaught C++ exception crossing PUC-Lua's C-compiled,
// longjmp-based lua_pcall is undefined behaviour.
void RunScriptFunctionRegistryExceptionSafetyTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script function exception safety host setup failed");

    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{ .name = "Tests.Throws" },
                           .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) -> kb::script::ScriptFunctionCallResult {
                               throw std::runtime_error("boom");
                           },
                       }),
        "Script function exception safety did not register Tests.Throws");

    int survivorCalls = 0;
    kb::tests::Require(host.RegisterFunction(kb::script::ScriptFunctionDesc{
                           .signature = kb::script::ScriptFunctionSignature{ .name = "Tests.Survivor" },
                           .callback = [&survivorCalls](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                               ++survivorCalls;
                               return kb::script::ScriptFunctionCallResult{ .executed = true };
                           },
                       }),
        "Script function exception safety did not register Tests.Survivor");

    const kb::script::ScriptFunctionCallResult throwingResult = host.Functions().Call("Tests.Throws", {}, kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(!throwingResult.Succeeded(), "Script function registry did not report a thrown exception as a failed call");
    bool sawExceptionMessage = false;
    for (const std::string& error : throwingResult.errors) {
        sawExceptionMessage = sawExceptionMessage || error.find("boom") != std::string::npos;
    }
    kb::tests::Require(sawExceptionMessage, "Script function registry did not surface the exception's message in the call result");

    // The registry itself must stay usable after a callback throws: no
    // corrupted state, no crash, the next call succeeds normally.
    const kb::script::ScriptFunctionCallResult survivorResult = host.Functions().Call("Tests.Survivor", {}, kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(survivorResult.Succeeded() && survivorCalls == 1, "Script function registry did not remain usable after a callback threw");
}

// LIB-010: a native lifecycle/event callback throwing must not abort the
// rest of that phase's behaviour dispatch loop (ScriptRuntime::
// DispatchSceneBehaviours) — one misbehaving behaviour reports a diagnostic
// instead of preventing every later-dispatched behaviour in the same phase
// from running at all.
void RunNativeScriptBackendExceptionSafetyTest() {
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kThrowingAsset{ 5101U };
    constexpr kb::assets::AssetId kSurvivorAsset{ 5102U };

    const kb::scene::SceneObject throwingObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ExceptionSafetyThrows" });
    scene.Components().Behaviours().Set(throwingObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kThrowingAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
        .executionOrder = 0,
    });
    const kb::scene::SceneObject survivorObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ExceptionSafetySurvivor" });
    scene.Components().Behaviours().Set(survivorObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kSurvivorAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
        .executionOrder = 1,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();

    kb::tests::Require(
        native->RegisterLifecycle(kThrowingAsset, kb::script::ScriptLifecycleEvent::Tick, [](kb::script::ScriptExecutionContext&) -> void {
            throw std::runtime_error("native boom");
        }),
        "Native exception safety did not register the throwing behaviour");
    int survivorTicks = 0;
    kb::tests::Require(
        native->RegisterLifecycle(kSurvivorAsset, kb::script::ScriptLifecycleEvent::Tick, [&survivorTicks](kb::script::ScriptExecutionContext&) {
            ++survivorTicks;
        }),
        "Native exception safety did not register the survivor behaviour");

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Native exception safety backend registration failed");

    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(!result.Succeeded(), "Native exception safety did not report a diagnostic for the throwing behaviour");
    kb::tests::Require(result.visitedBehaviours == 2U, "Native exception safety did not visit both behaviours");
    kb::tests::Require(result.executedBehaviours == 1U, "Native exception safety must not count the throwing behaviour as executed");
    kb::tests::Require(survivorTicks == 1, "Native exception safety must still dispatch the behaviour after the one that threw");
    bool sawExceptionMessage = false;
    bool sawLifecyclePhase = false;
    for (const kb::script::ScriptDiagnostic& diagnostic : result.diagnostics) {
        sawExceptionMessage = sawExceptionMessage || diagnostic.message.find("native boom") != std::string::npos;
        sawLifecyclePhase = sawLifecyclePhase || (diagnostic.lifecyclePhase.has_value() && *diagnostic.lifecyclePhase == kb::script::ScriptLifecycleEvent::Tick);
    }
    kb::tests::Require(sawExceptionMessage, "Native exception safety did not surface the exception's message in the diagnostics");
    kb::tests::Require(sawLifecyclePhase, "Native exception safety diagnostic must carry the lifecycle phase it failed in (LIB-036)");
}

// LIB-021: once the world has dispatched its first lifecycle phase, further
// Register() calls on that ScriptFunctionRegistry must be rejected — a
// function registered after the world starts running could be visible to
// some already-running dispatch paths (Lua sugar tables, compiled Visual
// Graph bindings, both generated/snapshotted at setup time) but not others.
// LIB-030: a function name that was never registered (not exposed) must
// never be callable — ScriptFunctionRegistry::Call, the single choke point
// every frontend (Native direct call, Lua's CallFunction, the Visual Graph
// CallNative node) funnels through, must reject it by name with a clear
// diagnostic instead of silently succeeding or falling through.
void RunUnexposedFunctionCannotBeCalledTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Unexposed function test host setup failed");
    kb::tests::Require(host.Functions().FindSignature("Tests.NeverRegistered") == nullptr, "Unexposed function test fixture must not already have this name registered");

    const kb::script::ScriptFunctionCallResult result = host.Functions().Call("Tests.NeverRegistered", {}, kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(!result.Succeeded(), "An unexposed function name must not be callable");
    kb::tests::Require(!result.executed, "An unexposed function name must not report executed=true");
    bool sawNotRegisteredMessage = false;
    for (const std::string& error : result.errors) {
        sawNotRegisteredMessage = sawNotRegisteredMessage || error.find("is not registered") != std::string::npos;
    }
    kb::tests::Require(sawNotRegisteredMessage, "Unexposed function call must report a clear 'not registered' diagnostic, not a silent failure");
}

void RunScriptFunctionRegistryLocksAfterFirstDispatchTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Registry lock test host setup failed");
    kb::tests::Require(!host.Functions().IsLocked(), "Script function registry must not be locked before any dispatch");

    kb::tests::Require(
        host.RegisterFunction(kb::script::ScriptFunctionDesc{
            .signature = kb::script::ScriptFunctionSignature{ .name = "Tests.BeforeStart" },
            .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
                return kb::script::ScriptFunctionCallResult{ .executed = true };
            },
        }),
        "Registry lock test could not register before dispatch");

    static_cast<void>(host.Runtime().ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F));
    kb::tests::Require(host.Functions().IsLocked(), "Script function registry must lock after the first lifecycle dispatch");

    const bool registeredAfterStart = host.RegisterFunction(kb::script::ScriptFunctionDesc{
        .signature = kb::script::ScriptFunctionSignature{ .name = "Tests.AfterStart" },
        .callback = [](const kb::script::ScriptFunctionCallContext&, std::span<const kb::script::ScriptFunctionArgument>) {
            return kb::script::ScriptFunctionCallResult{ .executed = true };
        },
    });
    kb::tests::Require(!registeredAfterStart, "Script function registry must reject registration after the world has started");
    kb::tests::Require(host.Functions().FindSignature("Tests.AfterStart") == nullptr, "Script function registry must not have added the rejected function");

    const kb::script::ScriptFunctionCallResult stillWorks = host.Functions().Call("Tests.BeforeStart", {}, kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(stillWorks.Succeeded(), "Script function registry must keep serving functions registered before the lock");
}

// LIB-038: a callback that calls back into ScriptFunctionRegistry::Call on
// the same registry (directly here; the same guard also covers a chain of
// distinct functions calling each other) must be rejected once the call
// depth limit is reached, with a clear diagnostic, instead of recursing
// until the native call stack overflows and crashes the process.
void RunScriptFunctionRegistryReentrancyGuardTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Reentrancy guard test host setup failed");

    kb::tests::Require(
        host.RegisterFunction(kb::script::ScriptFunctionDesc{
            .signature = kb::script::ScriptFunctionSignature{ .name = "Tests.Recurse" },
            .callback = [&host](const kb::script::ScriptFunctionCallContext& context, std::span<const kb::script::ScriptFunctionArgument>) {
                return host.Functions().Call("Tests.Recurse", {}, context);
            },
        }),
        "Reentrancy guard test could not register a self-recursive function");

    const kb::script::ScriptFunctionCallResult result = host.Functions().Call("Tests.Recurse", {}, kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(!result.Succeeded(), "A reentrant call chain past the depth limit must fail instead of overflowing the stack");
    bool sawDepthMessage = false;
    for (const std::string& error : result.errors) {
        sawDepthMessage = sawDepthMessage || error.find("maximum call depth") != std::string::npos;
    }
    kb::tests::Require(sawDepthMessage, "Reentrancy guard must report a clear 'maximum call depth' diagnostic, not a silent or generic failure");

    const kb::script::ScriptFunctionCallResult unrelatedStillWorks = host.Functions().Call("Tests.Recurse", {}, kb::script::ScriptFunctionCallContext{});
    kb::tests::Require(!unrelatedStillWorks.Succeeded(), "Depth guard must reject the same recursive chain deterministically on a later call");
    kb::tests::Require(host.Functions().FindSignature("Tests.Recurse") != nullptr, "The registry itself must remain intact and queryable after a rejected reentrant call chain");
}

// LIB-011: the full 10-event lifecycle order (Created, Activated, Ready,
// FixedTick, Tick, LateTick, BeforeRender, AfterRender, Deactivated,
// Destroyed) must hold for a Native behaviour end to end, not just the
// fragments other tests already cover. Created/Activated/Ready/
// FixedTick/Tick/LateTick/BeforeRender/AfterRender all happen within the
// first ExecuteFrame call; Deactivated/Destroyed only appear once the
// behaviour is removed and a second ExecuteFrame runs (see
// ScriptRuntimeSceneSystem::SyncBehaviourLifecycles).
void RunNativeFullLifecycleOrderTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kAsset{ 5201U };

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();

    std::vector<std::string> order;
    const auto record = [&order](std::string name) {
        return [&order, name = std::move(name)](kb::script::ScriptExecutionContext&) {
            order.push_back(name);
        };
    };
    kb::tests::Require(native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::Created, record("Created")), "Full lifecycle order Created registration failed");
    kb::tests::Require(native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::Activated, record("Activated")), "Full lifecycle order Activated registration failed");
    kb::tests::Require(native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::Ready, record("Ready")), "Full lifecycle order Ready registration failed");
    kb::tests::Require(native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::FixedTick, record("FixedTick")), "Full lifecycle order FixedTick registration failed");
    kb::tests::Require(native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::Tick, record("Tick")), "Full lifecycle order Tick registration failed");
    kb::tests::Require(native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::LateTick, record("LateTick")), "Full lifecycle order LateTick registration failed");
    kb::tests::Require(native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::BeforeRender, record("BeforeRender")), "Full lifecycle order BeforeRender registration failed");
    kb::tests::Require(native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::AfterRender, record("AfterRender")), "Full lifecycle order AfterRender registration failed");
    kb::tests::Require(native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::Deactivated, record("Deactivated")), "Full lifecycle order Deactivated registration failed");
    kb::tests::Require(native->RegisterLifecycle(kAsset, kb::script::ScriptLifecycleEvent::Destroyed, record("Destroyed")), "Full lifecycle order Destroyed registration failed");

    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Full lifecycle order backend registration failed");

    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FullLifecycleNative" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    kb::script::ScriptRuntimeSceneSystem system{ runtime };
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(system.LastResult().Succeeded(), "Full lifecycle order first frame produced diagnostics");

    const std::vector<std::string> expectedFirstFrame{
        "Created", "Activated", "Ready", "FixedTick", "Tick", "LateTick", "BeforeRender", "AfterRender",
    };
    kb::tests::Require(order == expectedFirstFrame, "Full lifecycle order did not dispatch Created..AfterRender in the documented order");

    scene.Components().Behaviours().Remove(object.Entity());
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(system.LastResult().Succeeded(), "Full lifecycle order second frame produced diagnostics");

    const std::vector<std::string> expectedFull{
        "Created", "Activated", "Ready", "FixedTick", "Tick", "LateTick", "BeforeRender", "AfterRender", "Deactivated", "Destroyed",
    };
    kb::tests::Require(order == expectedFull, "Full lifecycle order did not append Deactivated then Destroyed once the behaviour was removed");
}

// LIB-011: same guarantee as RunNativeFullLifecycleOrderTest, for a Lua
// behaviour. Uses SetShared/GetShared to build a cumulative order string
// rather than Emit(), because emitting an event literally named "Created"/
// "Tick"/... would be redispatched (ScriptRuntime::DrainEvents) to any
// handler function of that same name — including the lifecycle function
// itself — which is not what this test wants to exercise.
void RunPucLuaFullLifecycleOrderTest() {
    kb::script::PucLuaScriptRuntime luaRuntime;
    constexpr kb::assets::AssetId kLuaAsset{ 5301U };
    const kb::script::PucLuaLoadResult loaded = luaRuntime.LoadScript(kLuaAsset, R"(
local function record(name)
    SetShared("lifecycleOrder", (GetShared("lifecycleOrder") or "") .. name .. ";")
end
function Created(self, dt) record("Created") end
function Activated(self, dt) record("Activated") end
function Ready(self, dt) record("Ready") end
function FixedTick(self, dt) record("FixedTick") end
function Tick(self, dt) record("Tick") end
function LateTick(self, dt) record("LateTick") end
function BeforeRender(self, dt) record("BeforeRender") end
function AfterRender(self, dt) record("AfterRender") end
function Deactivated(self, dt) record("Deactivated") end
function Destroyed(self, dt) record("Destroyed") end
)",
        "FullLifecycle.lua");
    kb::tests::Require(loaded.succeeded, "Full lifecycle order Lua script did not load");

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(luaRuntime)), "Full lifecycle order Lua backend registration failed");

    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FullLifecycleLua" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kLuaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    kb::script::ScriptRuntimeSceneSystem system{ runtime };
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(system.LastResult().Succeeded(), "Full lifecycle order Lua first frame produced diagnostics");

    const std::optional<kb::script::ScriptValue> afterFirstFrame = runtime.SharedState().Get("lifecycleOrder");
    kb::tests::Require(
        afterFirstFrame.has_value() && afterFirstFrame->AsString() == "Created;Activated;Ready;FixedTick;Tick;LateTick;BeforeRender;AfterRender;",
        "Full lifecycle order Lua did not dispatch Created..AfterRender in the documented order");

    scene.Components().Behaviours().Remove(object.Entity());
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(system.LastResult().Succeeded(), "Full lifecycle order Lua second frame produced diagnostics");

    const std::optional<kb::script::ScriptValue> afterSecondFrame = runtime.SharedState().Get("lifecycleOrder");
    kb::tests::Require(
        afterSecondFrame.has_value() &&
            afterSecondFrame->AsString() == "Created;Activated;Ready;FixedTick;Tick;LateTick;BeforeRender;AfterRender;Deactivated;Destroyed;",
        "Full lifecycle order Lua did not append Deactivated then Destroyed once the behaviour was removed");
}

// LIB-011: same guarantee as the Native/Lua variants, for a Visual Graph
// behaviour: one graph with an Event+EmitEvent pair per lifecycle phase,
// each EmitEvent using a symbol that names no real event/function anywhere
// (so ScriptRuntime::DrainEvents redispatching it is a harmless no-op).
void RunVisualGraphFullLifecycleOrderTest() {
    struct PhaseNode {
        kb::visual::VisualGraphLifecycleEvent lifecycle;
        const char* emitName;
    };
    const std::array<PhaseNode, 10> phases{ {
        { kb::visual::VisualGraphLifecycleEvent::Created, "GraphCreated" },
        { kb::visual::VisualGraphLifecycleEvent::Activated, "GraphActivated" },
        { kb::visual::VisualGraphLifecycleEvent::Ready, "GraphReady" },
        { kb::visual::VisualGraphLifecycleEvent::FixedTick, "GraphFixedTick" },
        { kb::visual::VisualGraphLifecycleEvent::Tick, "GraphTick" },
        { kb::visual::VisualGraphLifecycleEvent::LateTick, "GraphLateTick" },
        { kb::visual::VisualGraphLifecycleEvent::BeforeRender, "GraphBeforeRender" },
        { kb::visual::VisualGraphLifecycleEvent::AfterRender, "GraphAfterRender" },
        { kb::visual::VisualGraphLifecycleEvent::Deactivated, "GraphDeactivated" },
        { kb::visual::VisualGraphLifecycleEvent::Destroyed, "GraphDestroyed" },
    } };

    kb::visual::VisualGraphAsset graph{};
    graph.name = "FullLifecycleGraph";
    std::uint32_t nextId = 1U;
    for (const PhaseNode& phase : phases) {
        const std::uint32_t eventNodeId = nextId++;
        const std::uint32_t emitNodeId = nextId++;
        graph.nodes.push_back(kb::visual::VisualGraphNode{ .id = eventNodeId, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = phase.lifecycle });
        graph.nodes.push_back(kb::visual::VisualGraphNode{ .id = emitNodeId, .kind = kb::visual::VisualGraphNodeKind::EmitEvent, .symbol = phase.emitName });
        graph.pins.push_back(kb::visual::VisualGraphPin{ .nodeId = eventNodeId, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void });
        graph.pins.push_back(kb::visual::VisualGraphPin{ .nodeId = emitNodeId, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void });
        graph.edges.push_back(kb::visual::VisualGraphEdge{ .fromNode = eventNodeId, .fromPin = "then", .toNode = emitNodeId, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution });
    }

    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Full lifecycle order graph did not compile");

    constexpr kb::assets::AssetId kGraphAsset{ 5401U };
    kb::visual::VisualGraphRuntimeRegistry artifacts;
    artifacts.Store(kb::visual::VisualGraphRuntimeArtifact{
        .assetId = kGraphAsset,
        .graphName = graph.name,
        .module = compiled.module,
    });
    kb::visual::VisualGraphRuntimeBindingRegistry bindings;
    kb::visual::VisualGraphBehaviourInstanceRegistry instances;

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(
        runtime.RegisterBackend(std::make_unique<kb::script::VisualGraphScriptBackend>(artifacts, bindings, instances)),
        "Full lifecycle order graph backend registration failed");

    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "FullLifecycleGraph" });
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kGraphAsset.value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    });

    kb::script::ScriptRuntimeSceneSystem system{ runtime };
    std::vector<std::string> order;

    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(system.LastResult().Succeeded(), "Full lifecycle order graph first frame produced diagnostics");
    for (const kb::script::ScriptEvent& event : system.LastResult().emittedEvents) {
        order.push_back(event.name);
    }

    scene.Components().Behaviours().Remove(object.Entity());
    static_cast<void>(system.ExecuteFrame(scene, 1.0F / 60.0F));
    kb::tests::Require(system.LastResult().Succeeded(), "Full lifecycle order graph second frame produced diagnostics");
    for (const kb::script::ScriptEvent& event : system.LastResult().emittedEvents) {
        order.push_back(event.name);
    }

    const std::vector<std::string> expected{
        "GraphCreated", "GraphActivated", "GraphReady", "GraphFixedTick", "GraphTick", "GraphLateTick", "GraphBeforeRender", "GraphAfterRender",
        "GraphDeactivated", "GraphDestroyed",
    };
    kb::tests::Require(order == expected, "Full lifecycle order graph did not dispatch every phase in the documented order");
}

// LIB-011 (parity): the guaranteed execution order (BehaviourExecutionOrderLess:
// TickGroup, then executionOrder, then entity id) must hold ACROSS backends,
// not just within one. Three behaviours — Native, Lua, Visual Graph — are
// created in a different order than their expected dispatch order and given
// distinct executionOrder values; only a real cross-backend sort could
// produce the expected sequence.
void RunCrossBackendLifecycleOrderParityTest() {
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kNativeAsset{ 5501U };
    constexpr kb::assets::AssetId kLuaAsset{ 5502U };
    constexpr kb::assets::AssetId kGraphAsset{ 5503U };

    const kb::scene::SceneObject graphObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ParityGraph" });
    const kb::scene::SceneObject nativeObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ParityNative" });
    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ParityLua" });
    scene.Components().Behaviours().Set(graphObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kGraphAsset.value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
        .executionOrder = 20,
    });
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

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    kb::script::NativeScriptBackend* native = nativeBackend.get();
    kb::tests::Require(
        native->RegisterLifecycle(kNativeAsset, kb::script::ScriptLifecycleEvent::Tick, [](kb::script::ScriptExecutionContext& context) {
            context.Emit("OrderMark", { kb::script::ScriptEventArgument{ .name = "who", .value = kb::script::ScriptValue{ std::string{ "Native" } } } });
        }),
        "Cross-backend lifecycle order parity Native registration failed");

    kb::script::PucLuaScriptRuntime luaRuntime;
    kb::tests::Require(luaRuntime.LoadScript(kLuaAsset, R"(
function Tick(self, dt)
    Emit("OrderMark", { who = "Lua" })
end
)",
                            "Parity.lua")
                            .succeeded,
        "Cross-backend lifecycle order parity Lua script did not load");

    kb::visual::VisualGraphAsset graph{};
    graph.name = "ParityGraph";
    graph.nodes = {
        kb::visual::VisualGraphNode{ .id = 1U, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick },
        kb::visual::VisualGraphNode{ .id = 2U, .kind = kb::visual::VisualGraphNodeKind::EmitEvent, .symbol = "GraphOrderMark" },
    };
    graph.pins = {
        kb::visual::VisualGraphPin{ .nodeId = 1U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{ .fromNode = 1U, .fromPin = "then", .toNode = 2U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
    };
    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Cross-backend lifecycle order parity graph did not compile");
    kb::visual::VisualGraphRuntimeRegistry artifacts;
    artifacts.Store(kb::visual::VisualGraphRuntimeArtifact{ .assetId = kGraphAsset, .graphName = graph.name, .module = compiled.module });
    kb::visual::VisualGraphRuntimeBindingRegistry bindings;
    kb::visual::VisualGraphBehaviourInstanceRegistry instances;

    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Cross-backend lifecycle order parity native backend registration failed");
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(luaRuntime)), "Cross-backend lifecycle order parity Lua backend registration failed");
    kb::tests::Require(
        runtime.RegisterBackend(std::make_unique<kb::script::VisualGraphScriptBackend>(artifacts, bindings, instances)),
        "Cross-backend lifecycle order parity graph backend registration failed");

    const kb::script::ScriptRuntimeExecutionResult result = runtime.ExecuteLifecycleAndDispatchEvents(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(result.Succeeded(), "Cross-backend lifecycle order parity produced diagnostics");

    std::vector<std::string> who;
    for (const kb::script::ScriptEvent& event : result.emittedEvents) {
        if (event.name == "OrderMark") {
            for (const kb::script::ScriptEventArgument& argument : event.arguments) {
                if (argument.name == "who") {
                    who.push_back(argument.value.AsString());
                }
            }
        } else if (event.name == "GraphOrderMark") {
            who.emplace_back("Graph");
        }
    }
    const std::vector<std::string> expected{ "Native", "Lua", "Graph" };
    kb::tests::Require(who == expected, "Cross-backend lifecycle dispatch must respect BehaviourExecutionOrderLess regardless of backend");
}

void RunScriptAudioApiTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    ProbeAudioPlaybackBackend audioBackend;
    kb::audio::AudioPlayback::RegisterBackend(scene, audioBackend);
    kb::tests::Require(host.Succeeded(), "Script audio API host did not initialize");
    kb::tests::Require(host.Functions().FindSignature("Audio.Play") != nullptr, "Script audio API did not register Audio.Play");
    kb::tests::Require(host.VisualGraphRuntimeBindings().Find(kb::visual::VisualGraphIrOpcode::CallNative, "Function.Audio.Play") != nullptr,
        "Script audio API did not register VisualGraph runtime binding");
    kb::tests::Require(host.VisualGraphNativeBindings().Find(kb::visual::VisualGraphIrOpcode::CallNative, "Function.Audio.Play") != nullptr,
        "Script audio API did not register VisualGraph native binding");

    const kb::assets::AssetId clipId{ 8801U };
    kb::tests::Require(scene.Assets().Manager().RegisterAsset(kb::assets::AssetMetadata{
                           .id = clipId,
                           .type = "AudioClip",
                           .name = "Ping",
                           .virtualPath = "/Game/Audio/Ping.wav",
                           .physicalPath = "Ping.wav",
                           .contentHash = 1U,
                       }),
        "Script audio API test clip asset registration failed");

    const kb::scene::SceneObject caller = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Audio Caller" });
    const std::vector<kb::script::ScriptFunctionArgument> directArguments{
        kb::script::ScriptFunctionArgument{ .name = "clip", .value = kb::script::ScriptValue{ std::string{ "/Game/Audio/Ping.wav" } } },
        kb::script::ScriptFunctionArgument{ .name = "volume", .value = kb::script::ScriptValue{ 0.25F } },
        kb::script::ScriptFunctionArgument{ .name = "pitch", .value = kb::script::ScriptValue{ 1.5F } },
        kb::script::ScriptFunctionArgument{ .name = "mute", .value = kb::script::ScriptValue{ true } },
        kb::script::ScriptFunctionArgument{ .name = "loop", .value = kb::script::ScriptValue{ true } },
        kb::script::ScriptFunctionArgument{ .name = "spatial", .value = kb::script::ScriptValue{ false } },
        kb::script::ScriptFunctionArgument{ .name = "pan", .value = kb::script::ScriptValue{ -0.5F } },
        kb::script::ScriptFunctionArgument{ .name = "spatialBlend", .value = kb::script::ScriptValue{ 0.25F } },
        kb::script::ScriptFunctionArgument{ .name = "attenuationModel", .value = kb::script::ScriptValue{ static_cast<int>(kb::audio::AudioAttenuationModel::Linear) } },
        kb::script::ScriptFunctionArgument{ .name = "minDistance", .value = kb::script::ScriptValue{ 2.0F } },
        kb::script::ScriptFunctionArgument{ .name = "maxDistance", .value = kb::script::ScriptValue{ 75.0F } },
        kb::script::ScriptFunctionArgument{ .name = "rolloff", .value = kb::script::ScriptValue{ 0.5F } },
        kb::script::ScriptFunctionArgument{ .name = "dopplerFactor", .value = kb::script::ScriptValue{ 0.1F } },
    };
    const kb::script::ScriptFunctionCallResult direct = host.Functions().Call(
        "Audio.Play",
        std::span<const kb::script::ScriptFunctionArgument>{ directArguments },
        kb::script::ScriptFunctionCallContext{
            .scene = &scene,
            .caller = caller.Entity(),
        });
    kb::tests::Require(direct.Succeeded(), "Script audio API direct Audio.Play call failed");
    const std::optional<kb::script::ScriptValue> directVoiceValue = direct.Output("voice");
    kb::tests::Require(directVoiceValue.has_value() && directVoiceValue->AsInt() == 1, "Script audio API direct call did not return a voice");
    kb::tests::Require(audioBackend.played.size() == 1U, "Script audio API direct call did not reach audio backend");
    const kb::audio::AudioPlayDesc& directPlay = audioBackend.played.back();
    kb::tests::Require(directPlay.clipAssetId == clipId.value, "Script audio API direct call sent the wrong clip id");
    kb::tests::Require(kb::tests::NearlyEqual(directPlay.volume, 0.25F), "Script audio API direct call did not preserve volume");
    kb::tests::Require(kb::tests::NearlyEqual(directPlay.pitch, 1.5F), "Script audio API direct call did not preserve pitch");
    kb::tests::Require(directPlay.mute && directPlay.loop && !directPlay.spatial, "Script audio API direct call did not preserve playback flags");
    kb::tests::Require(kb::tests::NearlyEqual(directPlay.pan, -0.5F) && kb::tests::NearlyEqual(directPlay.spatialBlend, 0.25F), "Script audio API direct call did not preserve pan or spatial blend");
    kb::tests::Require(directPlay.attenuationModel == kb::audio::AudioAttenuationModel::Linear && kb::tests::NearlyEqual(directPlay.minDistance, 2.0F) && kb::tests::NearlyEqual(directPlay.maxDistance, 75.0F) && kb::tests::NearlyEqual(directPlay.rolloff, 0.5F) && kb::tests::NearlyEqual(directPlay.dopplerFactor, 0.1F), "Script audio API direct call did not preserve attenuation settings");

    const kb::assets::AssetId luaAsset{ 8802U };
    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lua Audio Caller" });
    scene.Components().Behaviours().Set(luaObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = luaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    const kb::script::PucLuaLoadResult loadedLua = host.LuaRuntime().LoadScript(luaAsset, R"(
function Tick(self, dt)
    local voice, err = Audio.Play("/Game/Audio/Ping.wav", { volume = 0.75, spatial = false, pan = 0.5, spatialBlend = 0.6, attenuationModel = 3, minDistance = 4.0, maxDistance = 120.0, rolloff = 1.4, dopplerFactor = 0.2 })
    if voice == nil then
        Emit("AudioPlayFailed", { error = err })
        return
    end
    SetShared("luaAudioVoice", voice)
end
)");
    kb::tests::Require(loadedLua.succeeded, "Script audio API Lua wrapper script did not load");

    const kb::script::ScriptRuntimeExecutionResult tick = host.Runtime().ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.016F);
    kb::tests::Require(tick.Succeeded(), "Script audio API Lua wrapper execution failed");
    const std::optional<kb::script::ScriptValue> luaVoiceValue = host.SharedState().Get("luaAudioVoice");
    kb::tests::Require(luaVoiceValue.has_value() && luaVoiceValue->AsInt() == 2, "Script audio API Lua wrapper did not return a voice");
    kb::tests::Require(audioBackend.played.size() == 2U, "Script audio API Lua wrapper did not reach audio backend");
    const kb::audio::AudioPlayDesc& luaPlay = audioBackend.played.back();
    kb::tests::Require(luaPlay.clipAssetId == clipId.value, "Script audio API Lua wrapper sent the wrong clip id");
    kb::tests::Require(kb::tests::NearlyEqual(luaPlay.volume, 0.75F), "Script audio API Lua wrapper did not preserve volume");
    kb::tests::Require(!luaPlay.spatial, "Script audio API Lua wrapper did not preserve flags");
    kb::tests::Require(kb::tests::NearlyEqual(luaPlay.pan, 0.5F) && kb::tests::NearlyEqual(luaPlay.spatialBlend, 0.6F), "Script audio API Lua wrapper did not preserve pan or spatial blend");
    kb::tests::Require(luaPlay.attenuationModel == kb::audio::AudioAttenuationModel::Exponential && kb::tests::NearlyEqual(luaPlay.minDistance, 4.0F) && kb::tests::NearlyEqual(luaPlay.maxDistance, 120.0F) && kb::tests::NearlyEqual(luaPlay.rolloff, 1.4F) && kb::tests::NearlyEqual(luaPlay.dopplerFactor, 0.2F), "Script audio API Lua wrapper did not preserve attenuation settings");
    kb::audio::AudioPlayback::UnregisterBackend(scene, audioBackend);
}

void RunScriptWorldTimePhysicsApiTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "WorldApiProject";
    const std::filesystem::path prefabPath = projectRoot / "Assets" / "Prefabs" / "RuntimePrefab.kbprefab";
    {
        kb::scene::Scene prefabSource;
        const kb::scene::SceneObject prefabRoot = prefabSource.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Prefab Root" });
        kb::scene::TagsComponent prefabTags;
        kb::scene::SetTagsText(prefabTags, "Prefab, Runtime");
        prefabSource.Components().Tags().Set(prefabRoot.Entity(), prefabTags);
        const kb::scene::ScenePrefabHandle prefab = prefabSource.Prefabs().CaptureRegistered(prefabRoot, "RuntimePrefab");
        kb::tests::Require(prefabSource.Prefabs().Save(prefab, prefabPath), "Script world API prefab fixture was not saved");
    }

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Script world API project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Script world API prefab was not discovered");
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script world/time/physics API host did not initialize");
    kb::tests::Require(host.Functions().FindSignature("World.FindByName") != nullptr, "World.FindByName was not registered");
    kb::tests::Require(host.Functions().FindSignature("Time.Delta") != nullptr, "Time.Delta was not registered");
    kb::tests::Require(host.Functions().FindSignature("Physics.Raycast") != nullptr, "Physics.Raycast was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.GetPosition") != nullptr, "Transform.GetPosition was not registered");

    const kb::script::ScriptFunctionCallContext context{
        .scene = &scene,
        .deltaSeconds = 0.25F,
    };
    const std::vector<kb::script::ScriptFunctionArgument> spawnArgs{
        kb::script::ScriptFunctionArgument{ .name = "name", .value = kb::script::ScriptValue{ std::string{ "Enemy" } } },
        kb::script::ScriptFunctionArgument{ .name = "x", .value = kb::script::ScriptValue{ 2.0F } },
        kb::script::ScriptFunctionArgument{ .name = "y", .value = kb::script::ScriptValue{ 3.0F } },
        kb::script::ScriptFunctionArgument{ .name = "z", .value = kb::script::ScriptValue{ 4.0F } },
    };
    const kb::script::ScriptFunctionCallResult spawned = host.Functions().Call("World.Spawn", spawnArgs, context);
    kb::tests::Require(spawned.Succeeded(), "World.Spawn direct call failed");
    const std::optional<kb::script::ScriptValue> spawnedEntityValue = spawned.Output("entity");
    const kb::scene::SceneEntity enemy{ spawnedEntityValue.has_value() ? spawnedEntityValue->AsUInt64() : 0U };
    kb::tests::Require(enemy.IsValid() && scene.Entities().IsAlive(enemy), "World.Spawn did not create a live entity");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(enemy).localPosition.x, 2.0F)
            && kb::tests::NearlyEqual(scene.Transforms().Get(enemy).localPosition.y, 3.0F)
            && kb::tests::NearlyEqual(scene.Transforms().Get(enemy).localPosition.z, 4.0F),
        "World.Spawn did not apply direct spawn position");

    // LIB-065: World.Current/IsPlaying/FrameIndex/FixedStepIndex, called
    // the same way any other World.* function is (through the registry,
    // not just the underlying kb::scene::Scene API directly).
    const kb::script::ScriptFunctionCallResult currentResult = host.Functions().Call("World.Current", {}, context);
    kb::tests::Require(currentResult.Succeeded() && currentResult.Output("world").has_value() && currentResult.Output("world")->AsUInt64() == scene.Id(),
        "World.Current must return the calling scene's own runtime id");

    const kb::script::ScriptFunctionCallResult playingResult = host.Functions().Call("World.IsPlaying", {}, context);
    kb::tests::Require(playingResult.Succeeded() && playingResult.Output("playing").has_value() && playingResult.Output("playing")->AsBool(),
        "World.IsPlaying must report true for a scene that has not been explicitly paused");
    scene.Runtime().SetPlaying(false);
    const kb::script::ScriptFunctionCallResult pausedResult = host.Functions().Call("World.IsPlaying", {}, context);
    kb::tests::Require(pausedResult.Succeeded() && !pausedResult.Output("playing")->AsBool(), "World.IsPlaying must reflect Scene::Runtime().SetPlaying(false)");
    scene.Runtime().SetPlaying(true);

    const kb::script::ScriptFunctionCallResult frameBeforeUpdate = host.Functions().Call("World.FrameIndex", {}, context);
    kb::tests::Require(frameBeforeUpdate.Succeeded() && frameBeforeUpdate.Output("frame")->AsInt64() == 0, "World.FrameIndex must start at 0 before any Update()");
    static_cast<void>(scene.Runtime().Update(0.016F));
    const kb::script::ScriptFunctionCallResult frameAfterUpdate = host.Functions().Call("World.FrameIndex", {}, context);
    kb::tests::Require(frameAfterUpdate.Succeeded() && frameAfterUpdate.Output("frame")->AsInt64() == 1, "World.FrameIndex must reflect Scene::Runtime().FrameIndex() after an Update()");

    const kb::script::ScriptFunctionCallResult fixedStepResult = host.Functions().Call("World.FixedStepIndex", {}, context);
    kb::tests::Require(fixedStepResult.Succeeded() && fixedStepResult.Output("step")->AsInt64() == 0, "World.FixedStepIndex must start at 0 for a scene with no fixed-step systems");

    // LIB-066: World.Spawn(prefab, pose, parent?) — the prefab branch,
    // exercising a full pose (position AND rotation, not just position)
    // and an explicit parent, reusing the same "/Game/Prefabs/
    // RuntimePrefab.kbprefab" fixture World.InstantiatePrefab uses below.
    const std::vector<kb::script::ScriptFunctionArgument> spawnFromPrefabArgs{
        kb::script::ScriptFunctionArgument{ .name = "prefab", .value = kb::script::ScriptValue{ std::string{ "/Game/Prefabs/RuntimePrefab.kbprefab" } } },
        kb::script::ScriptFunctionArgument{ .name = "parent", .value = kb::script::ScriptValue{ enemy.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "x", .value = kb::script::ScriptValue{ 5.0F } },
        kb::script::ScriptFunctionArgument{ .name = "y", .value = kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ .name = "z", .value = kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ .name = "rotX", .value = kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ .name = "rotY", .value = kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ .name = "rotZ", .value = kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ .name = "rotW", .value = kb::script::ScriptValue{ 1.0F } },
    };
    const kb::script::ScriptFunctionCallResult spawnedFromPrefab = host.Functions().Call("World.Spawn", spawnFromPrefabArgs, context);
    kb::tests::Require(spawnedFromPrefab.Succeeded(), "World.Spawn(prefab=...) direct call failed");
    const kb::scene::SceneEntity spawnedPrefabRoot{ spawnedFromPrefab.Output("entity")->AsUInt64() };
    kb::tests::Require(spawnedPrefabRoot.IsValid() && scene.Entities().Name(spawnedPrefabRoot) == "Prefab Root", "World.Spawn(prefab=...) did not return the prefab's root entity");
    const kb::scene::TagsComponent* spawnedPrefabTags = scene.Components().Tags().TryGet(spawnedPrefabRoot);
    kb::tests::Require(spawnedPrefabTags != nullptr && kb::scene::TagsText(*spawnedPrefabTags) == "Prefab, Runtime", "World.Spawn(prefab=...) did not preserve the prefab's own component data");
    kb::tests::Require(scene.Hierarchy().Parent(spawnedPrefabRoot) == enemy, "World.Spawn(prefab=...) did not apply the requested parent");
    const kb::scene::TransformComponent spawnedPrefabTransform = scene.Transforms().Get(spawnedPrefabRoot);
    kb::tests::Require(kb::tests::NearlyEqual(spawnedPrefabTransform.localPosition.x, 5.0F), "World.Spawn(prefab=...) did not apply the requested local position");
    kb::tests::Require(kb::tests::NearlyEqual(spawnedPrefabTransform.localRotation.w, 1.0F), "World.Spawn(prefab=...) did not apply the requested rotation pose");
    // The "defined flush": world position must already reflect the parent
    // (enemy at x=2) plus the local offset (x=5) = 7, immediately after
    // World.Spawn returns — no separate Update()/SynchronizeTransforms()
    // call from the test should be required.
    kb::tests::Require(kb::tests::NearlyEqual(spawnedPrefabTransform.worldPosition.x, 7.0F),
        "World.Spawn(prefab=...) must return a handle whose WORLD position already reflects the parent hierarchy, not just local position, without a further flush from the caller");

    const std::vector<kb::script::ScriptFunctionArgument> translateArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ enemy.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "x", .value = kb::script::ScriptValue{ 1.0F } },
        kb::script::ScriptFunctionArgument{ .name = "y", .value = kb::script::ScriptValue{ -1.0F } },
        kb::script::ScriptFunctionArgument{ .name = "z", .value = kb::script::ScriptValue{ 0.5F } },
    };
    const kb::script::ScriptFunctionCallResult translated = host.Functions().Call("Transform.Translate", translateArgs, context);
    kb::tests::Require(translated.Succeeded() && translated.Output("moved").has_value() && translated.Output("moved")->AsBool(), "Transform.Translate direct call failed");
    const std::vector<kb::script::ScriptFunctionArgument> getEnemyPositionArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ enemy.Id(), kb::script::ScriptValueType::Entity } },
    };
    const kb::script::ScriptFunctionCallResult enemyPosition = host.Functions().Call("Transform.GetPosition", getEnemyPositionArgs, context);
    kb::tests::Require(enemyPosition.Succeeded() && enemyPosition.Output("found")->AsBool()
            && kb::tests::NearlyEqual(enemyPosition.Output("x")->AsFloat(), 3.0F)
            && kb::tests::NearlyEqual(enemyPosition.Output("y")->AsFloat(), 2.0F)
            && kb::tests::NearlyEqual(enemyPosition.Output("z")->AsFloat(), 4.5F),
        "Transform.GetPosition direct call returned the wrong translated position");

    const std::vector<kb::script::ScriptFunctionArgument> findArgs{
        kb::script::ScriptFunctionArgument{ .name = "name", .value = kb::script::ScriptValue{ std::string{ "Enemy" } } },
    };
    const kb::script::ScriptFunctionCallResult found = host.Functions().Call("World.FindByName", findArgs, context);
    kb::tests::Require(found.Succeeded() && found.Output("entity").has_value() && found.Output("entity")->AsUInt64() == enemy.Id(), "World.FindByName did not find the spawned entity");

    const std::vector<kb::script::ScriptFunctionArgument> setTagArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ enemy.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "tag", .value = kb::script::ScriptValue{ std::string{ "Enemy" } } },
    };
    const kb::script::ScriptFunctionCallResult tagged = host.Functions().Call("World.SetTag", setTagArgs, context);
    kb::tests::Require(tagged.Succeeded() && tagged.Output("tagged").has_value() && tagged.Output("tagged")->AsBool(), "World.SetTag direct call failed");
    const kb::scene::TagsComponent* enemyTags = scene.Components().Tags().TryGet(enemy);
    kb::tests::Require(enemyTags != nullptr && kb::scene::TagsText(*enemyTags) == "Enemy", "World.SetTag did not persist to scene TagsComponent");
    const std::vector<kb::script::ScriptFunctionArgument> findTagArgs{
        kb::script::ScriptFunctionArgument{ .name = "tag", .value = kb::script::ScriptValue{ std::string{ "Enemy" } } },
    };
    const kb::script::ScriptFunctionCallResult foundByTag = host.Functions().Call("World.FindByTag", findTagArgs, context);
    kb::tests::Require(foundByTag.Succeeded() && foundByTag.Output("entity").has_value() && foundByTag.Output("entity")->AsUInt64() == enemy.Id(), "World.FindByTag did not find the tagged entity");

    const kb::script::ScriptFunctionCallResult delta = host.Functions().Call("Time.Delta", {}, context);
    kb::tests::Require(delta.Succeeded() && delta.Output("delta").has_value() && kb::tests::NearlyEqual(delta.Output("delta")->AsFloat(), 0.25F), "Time.Delta direct call returned the wrong delta");

    const kb::scene::SceneObject floor = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Floor",
        .transform = kb::scene::TransformComponent{ .localPosition = kb::scene::Vec3{ 0.0F, 0.0F, 0.0F } },
    });
    scene.Components().Colliders().Set(floor.Entity(), kb::scene::ColliderComponent{
        .shape = kb::scene::ColliderShape::Box,
        .boxSize = kb::scene::Vec3{ 10.0F, 1.0F, 10.0F },
    });
    const std::vector<kb::script::ScriptFunctionArgument> rayArgs{
        kb::script::ScriptFunctionArgument{ .name = "originX", .value = kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ .name = "originY", .value = kb::script::ScriptValue{ 5.0F } },
        kb::script::ScriptFunctionArgument{ .name = "originZ", .value = kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ .name = "directionX", .value = kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ .name = "directionY", .value = kb::script::ScriptValue{ -1.0F } },
        kb::script::ScriptFunctionArgument{ .name = "directionZ", .value = kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ .name = "distance", .value = kb::script::ScriptValue{ 10.0F } },
    };
    const kb::script::ScriptFunctionCallResult ray = host.Functions().Call("Physics.Raycast", rayArgs, context);
    kb::tests::Require(ray.Succeeded() && ray.Output("hit").has_value() && ray.Output("hit")->AsBool(), "Physics.Raycast direct call did not hit the test floor");
    kb::tests::Require(ray.Output("entity").has_value() && ray.Output("entity")->AsUInt64() == floor.Entity().Id(), "Physics.Raycast direct call hit the wrong entity");
    kb::tests::Require(ray.Output("distance").has_value() && kb::tests::NearlyEqual(ray.Output("distance")->AsFloat(), 4.5F), "Physics.Raycast direct call returned the wrong distance");

    const std::vector<kb::script::ScriptFunctionArgument> prefabArgs{
        kb::script::ScriptFunctionArgument{ .name = "prefab", .value = kb::script::ScriptValue{ std::string{ "/Game/Prefabs/RuntimePrefab.kbprefab" } } },
        kb::script::ScriptFunctionArgument{ .name = "x", .value = kb::script::ScriptValue{ -2.0F } },
        kb::script::ScriptFunctionArgument{ .name = "y", .value = kb::script::ScriptValue{ 0.5F } },
        kb::script::ScriptFunctionArgument{ .name = "z", .value = kb::script::ScriptValue{ 7.0F } },
    };
    const kb::script::ScriptFunctionCallResult prefabInstance = host.Functions().Call("World.InstantiatePrefab", prefabArgs, context);
    kb::tests::Require(prefabInstance.Succeeded() && prefabInstance.Output("entity").has_value(), "World.InstantiatePrefab direct call failed");
    const kb::scene::SceneEntity prefabEntity{ prefabInstance.Output("entity")->AsUInt64() };
    kb::tests::Require(prefabEntity.IsValid() && scene.Entities().Name(prefabEntity) == "Prefab Root", "World.InstantiatePrefab returned the wrong root entity");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(prefabEntity).localPosition.x, -2.0F)
            && kb::tests::NearlyEqual(scene.Transforms().Get(prefabEntity).localPosition.y, 0.5F)
            && kb::tests::NearlyEqual(scene.Transforms().Get(prefabEntity).localPosition.z, 7.0F),
        "World.InstantiatePrefab did not apply direct root position");
    const kb::scene::TagsComponent* prefabTags = scene.Components().Tags().TryGet(prefabEntity);
    kb::tests::Require(prefabTags != nullptr && kb::scene::TagsText(*prefabTags) == "Prefab, Runtime", "World.InstantiatePrefab did not preserve prefab tags");

    const kb::assets::AssetId luaAsset{ 8810U };
    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lua World Caller" });
    scene.Components().Behaviours().Set(luaObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = luaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    const kb::script::PucLuaLoadResult loadedLua = host.LuaRuntime().LoadScript(luaAsset, R"(
function Tick(self, dt)
    local entity = World.Spawn({ name = "LuaSpawned", x = 1.0, y = 2.0, z = 3.0 })
    local found = World.FindByName("LuaSpawned")
    local spawnPosition = Transform.GetPosition(entity)
    Transform.Translate(entity, 2.0, -1.0, 0.5)
    local translatedPosition = Transform.GetPosition(entity)
    Transform.SetPosition({ entity = entity, x = -4.0, y = 8.0, z = 12.0 })
    local setPosition = Transform.GetPosition(entity)
    World.SetTag(entity, "LuaEnemy")
    local tagged = World.HasTag(entity, "LuaEnemy")
    local foundByTag = World.FindByTag("LuaEnemy")
    local hit = Physics.Raycast({
        originX = 0.0, originY = 5.0, originZ = 0.0,
        directionX = 0.0, directionY = -1.0, directionZ = 0.0,
        distance = 10.0
    })
    local prefabRoot = World.InstantiatePrefab({ prefab = "/Game/Prefabs/RuntimePrefab.kbprefab", x = 9.0, y = 10.0, z = 11.0 })
    SetShared("world.entity", entity)
    SetShared("world.found", found)
    SetShared("transform.spawnX", spawnPosition.x)
    SetShared("transform.spawnY", spawnPosition.y)
    SetShared("transform.spawnZ", spawnPosition.z)
    SetShared("transform.translatedX", translatedPosition.x)
    SetShared("transform.translatedY", translatedPosition.y)
    SetShared("transform.translatedZ", translatedPosition.z)
    SetShared("transform.setX", setPosition.x)
    SetShared("transform.setY", setPosition.y)
    SetShared("transform.setZ", setPosition.z)
    SetShared("world.tagged", tagged)
    SetShared("world.foundByTag", foundByTag)
    SetShared("world.existsBeforeDestroy", World.Exists(entity))
    SetShared("world.destroyed", World.Destroy(entity))
    SetShared("world.existsAfterDestroy", World.Exists(entity))
    SetShared("world.raycastHit", hit.hit)
    SetShared("world.raycastEntity", hit.entity)
    SetShared("world.raycastDistance", hit.distance)
    SetShared("world.prefabRoot", prefabRoot)
    SetShared("time.delta", Time.delta())
end
)");
    kb::tests::Require(loadedLua.succeeded, "Script world/time/physics Lua wrapper script did not load");
    const kb::script::ScriptRuntimeExecutionResult tick = host.Runtime().ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.125F);
    kb::tests::Require(tick.Succeeded(), "Script world/time/physics Lua wrapper execution failed");
    const std::optional<kb::script::ScriptValue> luaEntity = host.SharedState().Get("world.entity");
    const std::optional<kb::script::ScriptValue> luaFound = host.SharedState().Get("world.found");
    kb::tests::Require(luaEntity.has_value() && luaFound.has_value() && luaEntity->AsInt() == luaFound->AsInt(), "Lua World.FindByName did not find Lua-spawned entity");
    kb::tests::Require(kb::tests::NearlyEqual(host.SharedState().Get("transform.spawnX")->AsFloat(), 1.0F)
            && kb::tests::NearlyEqual(host.SharedState().Get("transform.spawnY")->AsFloat(), 2.0F)
            && kb::tests::NearlyEqual(host.SharedState().Get("transform.spawnZ")->AsFloat(), 3.0F),
        "Lua World.Spawn table position was not applied");
    kb::tests::Require(kb::tests::NearlyEqual(host.SharedState().Get("transform.translatedX")->AsFloat(), 3.0F)
            && kb::tests::NearlyEqual(host.SharedState().Get("transform.translatedY")->AsFloat(), 1.0F)
            && kb::tests::NearlyEqual(host.SharedState().Get("transform.translatedZ")->AsFloat(), 3.5F),
        "Lua Transform.Translate returned the wrong position");
    kb::tests::Require(kb::tests::NearlyEqual(host.SharedState().Get("transform.setX")->AsFloat(), -4.0F)
            && kb::tests::NearlyEqual(host.SharedState().Get("transform.setY")->AsFloat(), 8.0F)
            && kb::tests::NearlyEqual(host.SharedState().Get("transform.setZ")->AsFloat(), 12.0F),
        "Lua Transform.SetPosition returned the wrong position");
    kb::tests::Require(host.SharedState().Get("world.tagged")->AsBool(), "Lua World.HasTag did not see the assigned tag");
    kb::tests::Require(host.SharedState().Get("world.foundByTag")->AsInt() == luaEntity->AsInt(), "Lua World.FindByTag did not find the tagged entity");
    kb::tests::Require(host.SharedState().Get("world.existsBeforeDestroy")->AsBool(), "Lua World.Exists was false before destroy");
    kb::tests::Require(host.SharedState().Get("world.destroyed")->AsBool(), "Lua World.Destroy did not report success");
    kb::tests::Require(!host.SharedState().Get("world.existsAfterDestroy")->AsBool(), "Lua World.Exists was true after destroy");
    kb::tests::Require(host.SharedState().Get("world.raycastHit")->AsBool(), "Lua Physics.Raycast did not hit the test floor");
    kb::tests::Require(static_cast<std::uint64_t>(host.SharedState().Get("world.raycastEntity")->AsInt()) == floor.Entity().Id(), "Lua Physics.Raycast hit the wrong entity");
    kb::tests::Require(kb::tests::NearlyEqual(host.SharedState().Get("world.raycastDistance")->AsFloat(), 4.5F), "Lua Physics.Raycast returned the wrong distance");
    const kb::scene::SceneEntity luaPrefabRoot{ static_cast<std::uint64_t>(host.SharedState().Get("world.prefabRoot")->AsInt()) };
    kb::tests::Require(luaPrefabRoot.IsValid() && scene.Entities().Name(luaPrefabRoot) == "Prefab Root", "Lua World.InstantiatePrefab returned the wrong root entity");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(luaPrefabRoot).localPosition.x, 9.0F)
            && kb::tests::NearlyEqual(scene.Transforms().Get(luaPrefabRoot).localPosition.y, 10.0F)
            && kb::tests::NearlyEqual(scene.Transforms().Get(luaPrefabRoot).localPosition.z, 11.0F),
        "Lua World.InstantiatePrefab table position was not applied");
    const kb::scene::TagsComponent* luaPrefabTags = scene.Components().Tags().TryGet(luaPrefabRoot);
    kb::tests::Require(luaPrefabTags != nullptr && kb::scene::TagsText(*luaPrefabTags) == "Prefab, Runtime", "Lua World.InstantiatePrefab did not preserve prefab tags");
    kb::tests::Require(kb::tests::NearlyEqual(host.SharedState().Get("time.delta")->AsFloat(), 0.125F), "Lua Time.delta returned the wrong delta");
}

// LIB-085: Transform.LocalPosition/LocalRotation/LocalScale (get/set) and
// Transform.WorldPose/SetWorldPose. Deliberately its own fresh Scene/host
// (isolated fixture pattern, LIB-067) rather than folding into
// RunScriptWorldTimePhysicsApiTest above. The core of this test is
// SetWorldPose's world-to-local back-solve (LIB-085's only genuinely new
// capability — everything else here is thin ergonomics over pre-existing
// GetProperty/SetProperty-reachable data): set a WORLD pose on a CHILD
// entity whose PARENT has a non-trivial world transform (translated AND
// rotated, not just translated), then prove WorldPose(child) reads back
// the exact pose that was requested — a round-trip through the real
// inverse math (kb::math::Inverse, ScriptTransformApi::SetWorldPose), not
// just "it compiles".
void RunTransformApiLocalAndWorldPoseTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Transform API local/world pose test host did not initialize");
    kb::tests::Require(host.Functions().FindSignature("Transform.LocalPosition") != nullptr, "Transform.LocalPosition was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.SetLocalPosition") != nullptr, "Transform.SetLocalPosition was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.LocalRotation") != nullptr, "Transform.LocalRotation was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.SetLocalRotation") != nullptr, "Transform.SetLocalRotation was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.LocalScale") != nullptr, "Transform.LocalScale was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.SetLocalScale") != nullptr, "Transform.SetLocalScale was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.WorldPose") != nullptr, "Transform.WorldPose was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.SetWorldPose") != nullptr, "Transform.SetWorldPose was not registered");

    const kb::script::ScriptFunctionCallContext context{ .scene = &scene, .deltaSeconds = 0.016F };

    const kb::scene::SceneObject subject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "TransformApiSubject" });
    const kb::script::ScriptFunctionArgument entityArg{ .name = "entity", .value = kb::script::ScriptValue{ subject.Entity().Id(), kb::script::ScriptValueType::Entity } };
    const std::vector<kb::script::ScriptFunctionArgument> entityOnlyArgs{ entityArg };

    // LocalPosition/LocalRotation/LocalScale: plain get/set round trip.
    const std::vector<kb::script::ScriptFunctionArgument> setLocalPositionArgs{
        entityArg,
        kb::script::ScriptFunctionArgument{ "x", kb::script::ScriptValue{ 1.0F } },
        kb::script::ScriptFunctionArgument{ "y", kb::script::ScriptValue{ 2.0F } },
        kb::script::ScriptFunctionArgument{ "z", kb::script::ScriptValue{ 3.0F } },
    };
    const kb::script::ScriptFunctionCallResult setLocalPosition = host.Functions().Call("Transform.SetLocalPosition", setLocalPositionArgs, context);
    kb::tests::Require(setLocalPosition.Succeeded() && setLocalPosition.Output("moved")->AsBool(), "Transform.SetLocalPosition direct call failed");
    const kb::script::ScriptFunctionCallResult localPosition = host.Functions().Call("Transform.LocalPosition", entityOnlyArgs, context);
    kb::tests::Require(localPosition.Succeeded() && localPosition.Output("found")->AsBool()
            && kb::tests::NearlyEqual(localPosition.Output("x")->AsFloat(), 1.0F)
            && kb::tests::NearlyEqual(localPosition.Output("y")->AsFloat(), 2.0F)
            && kb::tests::NearlyEqual(localPosition.Output("z")->AsFloat(), 3.0F),
        "Transform.LocalPosition did not round-trip Transform.SetLocalPosition");

    const std::vector<kb::script::ScriptFunctionArgument> setLocalRotationArgs{
        entityArg,
        kb::script::ScriptFunctionArgument{ "x", kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ "y", kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ "z", kb::script::ScriptValue{ 0.7071068F } },
        kb::script::ScriptFunctionArgument{ "w", kb::script::ScriptValue{ 0.7071068F } },
    };
    const kb::script::ScriptFunctionCallResult setLocalRotation = host.Functions().Call("Transform.SetLocalRotation", setLocalRotationArgs, context);
    kb::tests::Require(setLocalRotation.Succeeded() && setLocalRotation.Output("moved")->AsBool(), "Transform.SetLocalRotation direct call failed");
    const kb::script::ScriptFunctionCallResult localRotation = host.Functions().Call("Transform.LocalRotation", entityOnlyArgs, context);
    kb::tests::Require(localRotation.Succeeded() && localRotation.Output("found")->AsBool()
            && kb::tests::NearlyEqual(localRotation.Output("z")->AsFloat(), 0.7071068F)
            && kb::tests::NearlyEqual(localRotation.Output("w")->AsFloat(), 0.7071068F),
        "Transform.LocalRotation did not round-trip Transform.SetLocalRotation");

    const std::vector<kb::script::ScriptFunctionArgument> setLocalScaleArgs{
        entityArg,
        kb::script::ScriptFunctionArgument{ "x", kb::script::ScriptValue{ 2.0F } },
        kb::script::ScriptFunctionArgument{ "y", kb::script::ScriptValue{ 3.0F } },
        kb::script::ScriptFunctionArgument{ "z", kb::script::ScriptValue{ 4.0F } },
    };
    const kb::script::ScriptFunctionCallResult setLocalScale = host.Functions().Call("Transform.SetLocalScale", setLocalScaleArgs, context);
    kb::tests::Require(setLocalScale.Succeeded() && setLocalScale.Output("moved")->AsBool(), "Transform.SetLocalScale direct call failed");
    const kb::script::ScriptFunctionCallResult localScale = host.Functions().Call("Transform.LocalScale", entityOnlyArgs, context);
    kb::tests::Require(localScale.Succeeded() && localScale.Output("found")->AsBool()
            && kb::tests::NearlyEqual(localScale.Output("x")->AsFloat(), 2.0F)
            && kb::tests::NearlyEqual(localScale.Output("y")->AsFloat(), 3.0F)
            && kb::tests::NearlyEqual(localScale.Output("z")->AsFloat(), 4.0F),
        "Transform.LocalScale did not round-trip Transform.SetLocalScale");

    // WorldPose on a ROOT entity must equal its local pose directly
    // (TransformMath::ComposeRoot copies local straight to world unchanged).
    static_cast<void>(scene.Runtime().Update(0.0F));
    const kb::script::ScriptFunctionCallResult subjectWorldPose = host.Functions().Call("Transform.WorldPose", entityOnlyArgs, context);
    kb::tests::Require(subjectWorldPose.Succeeded() && subjectWorldPose.Output("found")->AsBool()
            && kb::tests::NearlyEqual(subjectWorldPose.Output("posX")->AsFloat(), 1.0F)
            && kb::tests::NearlyEqual(subjectWorldPose.Output("posY")->AsFloat(), 2.0F)
            && kb::tests::NearlyEqual(subjectWorldPose.Output("posZ")->AsFloat(), 3.0F)
            && kb::tests::NearlyEqual(subjectWorldPose.Output("rotZ")->AsFloat(), 0.7071068F)
            && kb::tests::NearlyEqual(subjectWorldPose.Output("rotW")->AsFloat(), 0.7071068F),
        "Transform.WorldPose for a root entity must equal its local pose directly");

    // SetWorldPose's genuinely new capability: back-solve a CHILD's local
    // pose from a requested WORLD pose, given a PARENT with a non-trivial
    // world transform (translated AND rotated).
    const kb::scene::SceneObject parentObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "TransformApiParent" });
    kb::scene::TransformComponent parentTransform = scene.Transforms().Get(parentObject.Entity());
    parentTransform.localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F };
    parentTransform.localRotation = kb::scene::Quat{ 0.0F, 0.0F, 0.7071068F, 0.7071068F }; // 90 degrees around +Z.
    scene.Transforms().Set(parentObject.Entity(), parentTransform);

    const kb::scene::SceneObject childObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "TransformApiChild" });
    kb::tests::Require(scene.Hierarchy().SetParent(childObject.Entity(), parentObject.Entity()), "Transform API world pose test could not parent the child fixture");
    const kb::script::ScriptFunctionArgument childEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ childObject.Entity().Id(), kb::script::ScriptValueType::Entity } };
    const std::vector<kb::script::ScriptFunctionArgument> childEntityOnlyArgs{ childEntityArg };

    const std::vector<kb::script::ScriptFunctionArgument> setWorldPoseArgs{
        childEntityArg,
        kb::script::ScriptFunctionArgument{ "posX", kb::script::ScriptValue{ 5.0F } },
        kb::script::ScriptFunctionArgument{ "posY", kb::script::ScriptValue{ 6.0F } },
        kb::script::ScriptFunctionArgument{ "posZ", kb::script::ScriptValue{ 7.0F } },
        kb::script::ScriptFunctionArgument{ "rotX", kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ "rotY", kb::script::ScriptValue{ 0.7071068F } },
        kb::script::ScriptFunctionArgument{ "rotZ", kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ "rotW", kb::script::ScriptValue{ 0.7071068F } },
    };
    const kb::script::ScriptFunctionCallResult setWorldPose = host.Functions().Call("Transform.SetWorldPose", setWorldPoseArgs, context);
    kb::tests::Require(setWorldPose.Succeeded() && setWorldPose.Output("moved")->AsBool(), "Transform.SetWorldPose direct call failed");

    // The local pose must actually have CHANGED from the requested world
    // values verbatim — proof the back-solve genuinely computed something,
    // not merely copied world into local (which would be wrong given this
    // parent is neither at the origin nor unrotated).
    const kb::scene::TransformComponent childTransformAfterSet = scene.Transforms().Get(childObject.Entity());
    kb::tests::Require(!kb::tests::NearlyEqual(childTransformAfterSet.localPosition.x, 5.0F) || !kb::tests::NearlyEqual(childTransformAfterSet.localRotation.w, 0.7071068F),
        "Transform.SetWorldPose must back-solve a genuinely different LOCAL pose from the requested WORLD pose for a non-trivial parent, not just copy world into local");

    // The real proof: reading WorldPose back immediately (SetWorldPose's
    // own flush, no separate Update() from the test) must report the
    // EXACT world pose that was requested.
    const kb::script::ScriptFunctionCallResult childWorldPoseAfterSet = host.Functions().Call("Transform.WorldPose", childEntityOnlyArgs, context);
    kb::tests::Require(childWorldPoseAfterSet.Succeeded() && childWorldPoseAfterSet.Output("found")->AsBool()
            && kb::tests::NearlyEqual(childWorldPoseAfterSet.Output("posX")->AsFloat(), 5.0F)
            && kb::tests::NearlyEqual(childWorldPoseAfterSet.Output("posY")->AsFloat(), 6.0F)
            && kb::tests::NearlyEqual(childWorldPoseAfterSet.Output("posZ")->AsFloat(), 7.0F)
            && kb::tests::NearlyEqual(childWorldPoseAfterSet.Output("rotY")->AsFloat(), 0.7071068F)
            && kb::tests::NearlyEqual(childWorldPoseAfterSet.Output("rotW")->AsFloat(), 0.7071068F),
        "Transform.SetWorldPose followed immediately by Transform.WorldPose must round-trip to the exact requested world pose, without a separate flush from the caller");

    // Dead entity: every function must fail cleanly, not throw or fabricate data.
    scene.Entities().Destroy(childObject.Entity());
    const kb::script::ScriptFunctionCallResult deadLocalPosition = host.Functions().Call("Transform.LocalPosition", childEntityOnlyArgs, context);
    kb::tests::Require(deadLocalPosition.Succeeded() && !deadLocalPosition.Output("found")->AsBool(), "Transform.LocalPosition on a dead entity must report found=false, not throw");
    const kb::script::ScriptFunctionCallResult deadSetWorldPose = host.Functions().Call("Transform.SetWorldPose", setWorldPoseArgs, context);
    kb::tests::Require(deadSetWorldPose.Succeeded() && !deadSetWorldPose.Output("moved")->AsBool(), "Transform.SetWorldPose on a dead entity must report moved=false, not throw");
}

// LIB-086: Transform.Parent (read) and Transform.SetParent(entity, parent,
// keepWorld). Deliberately its own fresh Scene/host (isolated fixture
// pattern, LIB-067). Proves: (1) Transform.Parent reports the real parent
// (invalid for a root, the actual parent for a child); (2) SetParent
// WITHOUT keepWorld only reassigns the parent relationship, leaving local
// pose untouched — the entity's WORLD pose genuinely changes (the "jump"
// LIB-086's own research confirmed is today's existing behavior); (3)
// SetParent WITH keepWorld preserves the WORLD pose across the reparent by
// back-solving a different local pose (reusing SetWorldPose's own
// WorldPoseToLocal math); (4) cycle detection — inherited unchanged from
// kb::scene::SceneHierarchyParenting::WouldCreateCycle, NOT reimplemented
// here — correctly rejects parenting an entity under its own descendant.
void RunTransformApiParentAndHierarchyTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Transform API parent/hierarchy test host did not initialize");
    kb::tests::Require(host.Functions().FindSignature("Transform.Parent") != nullptr, "Transform.Parent was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.SetParent") != nullptr, "Transform.SetParent was not registered");

    const kb::script::ScriptFunctionCallContext context{ .scene = &scene, .deltaSeconds = 0.016F };

    // Chain: grandparent -> parent -> child (all roots initially).
    const kb::scene::SceneObject grandparentObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "HierarchyGrandparent" });
    const kb::scene::SceneObject parentObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "HierarchyParent" });
    const kb::scene::SceneObject childObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "HierarchyChild" });
    kb::tests::Require(scene.Hierarchy().SetParent(parentObject.Entity(), grandparentObject.Entity()), "Hierarchy fixture could not parent parent->grandparent");
    kb::tests::Require(scene.Hierarchy().SetParent(childObject.Entity(), parentObject.Entity()), "Hierarchy fixture could not parent child->parent");

    const kb::script::ScriptFunctionArgument grandparentEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ grandparentObject.Entity().Id(), kb::script::ScriptValueType::Entity } };
    const kb::script::ScriptFunctionArgument childEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ childObject.Entity().Id(), kb::script::ScriptValueType::Entity } };
    const std::vector<kb::script::ScriptFunctionArgument> grandparentOnlyArgs{ grandparentEntityArg };
    const std::vector<kb::script::ScriptFunctionArgument> childOnlyArgs{ childEntityArg };

    // Transform.Parent: a root reports found=true with an invalid parent;
    // a child reports its real parent.
    const kb::script::ScriptFunctionCallResult grandparentParent = host.Functions().Call("Transform.Parent", grandparentOnlyArgs, context);
    kb::tests::Require(grandparentParent.Succeeded() && grandparentParent.Output("found")->AsBool() && !kb::scene::SceneEntity{ grandparentParent.Output("parent")->AsUInt64() }.IsValid(),
        "Transform.Parent for a root entity must report found=true with an invalid parent");
    const kb::script::ScriptFunctionCallResult childParent = host.Functions().Call("Transform.Parent", childOnlyArgs, context);
    kb::tests::Require(childParent.Succeeded() && childParent.Output("found")->AsBool() && childParent.Output("parent")->AsUInt64() == parentObject.Entity().Id(),
        "Transform.Parent for a child entity must report its real parent");

    // SetParent WITHOUT keepWorld: reparent grandchild directly under
    // grandparent — local pose must stay EXACTLY as it was (kb::scene never
    // touches local* on a plain reparent), so the WORLD pose must actually
    // CHANGE (grandparent has a different world transform than the old
    // parent did, by construction: grandparent is at the origin, unrelated
    // to childObject's local offset).
    kb::scene::TransformComponent parentTransformSetup = scene.Transforms().Get(parentObject.Entity());
    parentTransformSetup.localPosition = kb::scene::Vec3{ 20.0F, 0.0F, 0.0F };
    scene.Transforms().Set(parentObject.Entity(), parentTransformSetup);
    kb::scene::TransformComponent childTransformSetup = scene.Transforms().Get(childObject.Entity());
    childTransformSetup.localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F };
    scene.Transforms().Set(childObject.Entity(), childTransformSetup);
    static_cast<void>(scene.Runtime().Update(0.0F));
    const float childWorldXBeforeReparent = scene.Transforms().Get(childObject.Entity()).worldPosition.x;
    kb::tests::Require(kb::tests::NearlyEqual(childWorldXBeforeReparent, 21.0F), "Hierarchy fixture setup: child's world X must reflect parent(20) + local(1) before any reparent");

    const std::vector<kb::script::ScriptFunctionArgument> reparentNoKeepWorldArgs{
        childEntityArg,
        kb::script::ScriptFunctionArgument{ "parent", kb::script::ScriptValue{ grandparentObject.Entity().Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ "keepWorld", kb::script::ScriptValue{ false } },
    };
    const kb::script::ScriptFunctionCallResult reparentNoKeepWorld = host.Functions().Call("Transform.SetParent", reparentNoKeepWorldArgs, context);
    kb::tests::Require(reparentNoKeepWorld.Succeeded() && reparentNoKeepWorld.Output("moved")->AsBool(), "Transform.SetParent (no keepWorld) direct call failed");
    const kb::script::ScriptFunctionCallResult parentAfterNoKeepWorld = host.Functions().Call("Transform.Parent", childOnlyArgs, context);
    kb::tests::Require(parentAfterNoKeepWorld.Output("parent")->AsUInt64() == grandparentObject.Entity().Id(), "Transform.SetParent (no keepWorld) must actually reassign the parent");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(childObject.Entity()).localPosition.x, 1.0F),
        "Transform.SetParent WITHOUT keepWorld must leave the local pose untouched");
    // SetParent WITHOUT keepWorld does not force a sync itself (same lazy
    // convention as a plain kb::scene reparent) — force one here before
    // reading worldPosition, so this assertion checks the ACTUAL composed
    // world pose under the new parent, not a stale cached value from
    // before the reparent.
    static_cast<void>(scene.Runtime().Update(0.0F));
    kb::tests::Require(!kb::tests::NearlyEqual(scene.Transforms().Get(childObject.Entity()).worldPosition.x, childWorldXBeforeReparent),
        "Transform.SetParent WITHOUT keepWorld must let the WORLD pose change (the entity 'jumps' under the new parent, since only the parent relationship changed)");

    // SetParent WITH keepWorld: reparent back under the ORIGINAL parent —
    // the WORLD pose must be preserved (back-solved local compensates),
    // even though the local pose is now different from either prior value.
    const std::vector<kb::script::ScriptFunctionArgument> reparentKeepWorldArgs{
        childEntityArg,
        kb::script::ScriptFunctionArgument{ "parent", kb::script::ScriptValue{ parentObject.Entity().Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ "keepWorld", kb::script::ScriptValue{ true } },
    };
    const float childWorldXBeforeKeepWorldReparent = scene.Transforms().Get(childObject.Entity()).worldPosition.x;
    const kb::script::ScriptFunctionCallResult reparentKeepWorld = host.Functions().Call("Transform.SetParent", reparentKeepWorldArgs, context);
    kb::tests::Require(reparentKeepWorld.Succeeded() && reparentKeepWorld.Output("moved")->AsBool(), "Transform.SetParent (keepWorld) direct call failed");
    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(childObject.Entity()).worldPosition.x, childWorldXBeforeKeepWorldReparent),
        "Transform.SetParent WITH keepWorld must preserve the entity's WORLD pose across the reparent");
    kb::tests::Require(!kb::tests::NearlyEqual(scene.Transforms().Get(childObject.Entity()).localPosition.x, 1.0F),
        "Transform.SetParent WITH keepWorld must back-solve a genuinely different LOCAL pose to compensate for the new parent, not just copy the old local value");

    // Cycle detection: attempting to parent grandparent (an ANCESTOR of
    // child, now child's parent again after the keepWorld reparent) under
    // child (its own DESCENDANT) must be rejected — inherited unchanged
    // from kb::scene::SceneHierarchyParenting::WouldCreateCycle, not
    // reimplemented in this script-layer wrapper.
    const std::vector<kb::script::ScriptFunctionArgument> cycleArgs{
        grandparentEntityArg,
        kb::script::ScriptFunctionArgument{ "parent", kb::script::ScriptValue{ childObject.Entity().Id(), kb::script::ScriptValueType::Entity } },
    };
    const kb::script::ScriptFunctionCallResult cycleAttempt = host.Functions().Call("Transform.SetParent", cycleArgs, context);
    kb::tests::Require(cycleAttempt.Succeeded() && !cycleAttempt.Output("moved")->AsBool(), "Transform.SetParent must reject parenting an entity under its own descendant (cycle)");
    const kb::script::ScriptFunctionCallResult grandparentParentAfterCycleAttempt = host.Functions().Call("Transform.Parent", grandparentOnlyArgs, context);
    kb::tests::Require(!kb::scene::SceneEntity{ grandparentParentAfterCycleAttempt.Output("parent")->AsUInt64() }.IsValid(),
        "A rejected cyclic SetParent must leave the hierarchy unchanged — grandparent must still be a root");

    // Dead entity: every function must fail cleanly, not throw.
    scene.Entities().Destroy(childObject.Entity());
    const kb::script::ScriptFunctionCallResult deadParent = host.Functions().Call("Transform.Parent", childOnlyArgs, context);
    kb::tests::Require(deadParent.Succeeded() && !deadParent.Output("found")->AsBool(), "Transform.Parent on a dead entity must report found=false, not throw");
    const kb::script::ScriptFunctionCallResult deadSetParent = host.Functions().Call("Transform.SetParent", reparentKeepWorldArgs, context);
    kb::tests::Require(deadSetParent.Succeeded() && !deadSetParent.Output("moved")->AsBool(), "Transform.SetParent on a dead entity must report moved=false, not throw");
}

// LIB-087: Transform.ChildCount/GetChild/FindChild — the index-and-loop
// convention over kb::scene's already O(1)-indexed hierarchy child storage
// (SceneHierarchyCache, exposed through SceneHierarchyAccess::ChildCount/
// ChildAt). Deliberately its own fresh Scene/host (isolated fixture
// pattern, LIB-067). Proves: real counts/lookups against a real
// multi-child fixture (including a DUPLICATE name, to prove FindChild's
// skip parameter genuinely walks past a repeat rather than always
// returning the first match), out-of-range/negative index handling, and
// dead-entity handling for all three functions.
void RunTransformApiChildIterationTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Transform API child iteration test host did not initialize");
    kb::tests::Require(host.Functions().FindSignature("Transform.ChildCount") != nullptr, "Transform.ChildCount was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.GetChild") != nullptr, "Transform.GetChild was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.FindChild") != nullptr, "Transform.FindChild was not registered");

    const kb::script::ScriptFunctionCallContext context{ .scene = &scene, .deltaSeconds = 0.016F };

    const kb::scene::SceneObject parentObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ChildIterationParent" });
    const kb::scene::SceneObject firstChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Foo" });
    const kb::scene::SceneObject secondChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Bar" });
    const kb::scene::SceneObject thirdChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Foo" }); // Duplicate name, on purpose.
    kb::tests::Require(scene.Hierarchy().SetParent(firstChild.Entity(), parentObject.Entity()), "Child iteration fixture could not parent firstChild");
    kb::tests::Require(scene.Hierarchy().SetParent(secondChild.Entity(), parentObject.Entity()), "Child iteration fixture could not parent secondChild");
    kb::tests::Require(scene.Hierarchy().SetParent(thirdChild.Entity(), parentObject.Entity()), "Child iteration fixture could not parent thirdChild");

    const kb::script::ScriptFunctionArgument parentEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ parentObject.Entity().Id(), kb::script::ScriptValueType::Entity } };
    const std::vector<kb::script::ScriptFunctionArgument> parentOnlyArgs{ parentEntityArg };
    const kb::script::ScriptFunctionArgument leafEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ firstChild.Entity().Id(), kb::script::ScriptValueType::Entity } };
    const std::vector<kb::script::ScriptFunctionArgument> leafOnlyArgs{ leafEntityArg };

    // ChildCount: 3 for the parent, 0 for a childless leaf (found=true
    // either way — childless is not the same as "not found").
    const kb::script::ScriptFunctionCallResult parentChildCount = host.Functions().Call("Transform.ChildCount", parentOnlyArgs, context);
    kb::tests::Require(parentChildCount.Succeeded() && parentChildCount.Output("found")->AsBool() && parentChildCount.Output("count")->AsInt() == 3,
        "Transform.ChildCount must report the real number of children (3)");
    const kb::script::ScriptFunctionCallResult leafChildCount = host.Functions().Call("Transform.ChildCount", leafOnlyArgs, context);
    kb::tests::Require(leafChildCount.Succeeded() && leafChildCount.Output("found")->AsBool() && leafChildCount.Output("count")->AsInt() == 0,
        "Transform.ChildCount for a childless (but alive) entity must report found=true, count=0");

    // GetChild: indices 0/1/2 in insertion order, 3 out of range, -1 invalid.
    for (int index = 0; index < 3; ++index) {
        const std::vector<kb::script::ScriptFunctionArgument> getChildArgs{
            parentEntityArg,
            kb::script::ScriptFunctionArgument{ "index", kb::script::ScriptValue{ index } },
        };
        const kb::script::ScriptFunctionCallResult getChild = host.Functions().Call("Transform.GetChild", getChildArgs, context);
        kb::tests::Require(getChild.Succeeded() && getChild.Output("found")->AsBool(), "Transform.GetChild must find every in-range index");
    }
    const std::vector<kb::script::ScriptFunctionArgument> getChild0Args{ parentEntityArg, kb::script::ScriptFunctionArgument{ "index", kb::script::ScriptValue{ 0 } } };
    const kb::script::ScriptFunctionCallResult getChild0 = host.Functions().Call("Transform.GetChild", getChild0Args, context);
    kb::tests::Require(getChild0.Output("child")->AsUInt64() == firstChild.Entity().Id(), "Transform.GetChild(0) must return the FIRST child added, in insertion order");
    const std::vector<kb::script::ScriptFunctionArgument> getChildOutOfRangeArgs{ parentEntityArg, kb::script::ScriptFunctionArgument{ "index", kb::script::ScriptValue{ 3 } } };
    const kb::script::ScriptFunctionCallResult getChildOutOfRange = host.Functions().Call("Transform.GetChild", getChildOutOfRangeArgs, context);
    kb::tests::Require(getChildOutOfRange.Succeeded() && !getChildOutOfRange.Output("found")->AsBool(), "Transform.GetChild must report found=false for an out-of-range index");
    const std::vector<kb::script::ScriptFunctionArgument> getChildNegativeArgs{ parentEntityArg, kb::script::ScriptFunctionArgument{ "index", kb::script::ScriptValue{ -1 } } };
    const kb::script::ScriptFunctionCallResult getChildNegative = host.Functions().Call("Transform.GetChild", getChildNegativeArgs, context);
    kb::tests::Require(getChildNegative.Succeeded() && !getChildNegative.Output("found")->AsBool(), "Transform.GetChild must report found=false for a negative index, not underflow");

    // FindChild: "Bar" is unique; "Foo" is duplicated — skip must walk past
    // the first match to the second, and a third request must fail.
    const std::vector<kb::script::ScriptFunctionArgument> findBarArgs{ parentEntityArg, kb::script::ScriptFunctionArgument{ "name", kb::script::ScriptValue{ std::string{ "Bar" } } } };
    const kb::script::ScriptFunctionCallResult findBar = host.Functions().Call("Transform.FindChild", findBarArgs, context);
    kb::tests::Require(findBar.Succeeded() && findBar.Output("found")->AsBool() && findBar.Output("child")->AsUInt64() == secondChild.Entity().Id(),
        "Transform.FindChild must find the uniquely-named child");

    const std::vector<kb::script::ScriptFunctionArgument> findFooFirstArgs{ parentEntityArg, kb::script::ScriptFunctionArgument{ "name", kb::script::ScriptValue{ std::string{ "Foo" } } } };
    const kb::script::ScriptFunctionCallResult findFooFirst = host.Functions().Call("Transform.FindChild", findFooFirstArgs, context);
    kb::tests::Require(findFooFirst.Succeeded() && findFooFirst.Output("found")->AsBool() && findFooFirst.Output("child")->AsUInt64() == firstChild.Entity().Id(),
        "Transform.FindChild without skip must return the FIRST match");

    const std::vector<kb::script::ScriptFunctionArgument> findFooSkip1Args{
        parentEntityArg,
        kb::script::ScriptFunctionArgument{ "name", kb::script::ScriptValue{ std::string{ "Foo" } } },
        kb::script::ScriptFunctionArgument{ "skip", kb::script::ScriptValue{ 1 } },
    };
    const kb::script::ScriptFunctionCallResult findFooSkip1 = host.Functions().Call("Transform.FindChild", findFooSkip1Args, context);
    kb::tests::Require(findFooSkip1.Succeeded() && findFooSkip1.Output("found")->AsBool() && findFooSkip1.Output("child")->AsUInt64() == thirdChild.Entity().Id(),
        "Transform.FindChild with skip=1 must walk past the first duplicate to the SECOND match");

    const std::vector<kb::script::ScriptFunctionArgument> findFooSkip2Args{
        parentEntityArg,
        kb::script::ScriptFunctionArgument{ "name", kb::script::ScriptValue{ std::string{ "Foo" } } },
        kb::script::ScriptFunctionArgument{ "skip", kb::script::ScriptValue{ 2 } },
    };
    const kb::script::ScriptFunctionCallResult findFooSkip2 = host.Functions().Call("Transform.FindChild", findFooSkip2Args, context);
    kb::tests::Require(findFooSkip2.Succeeded() && !findFooSkip2.Output("found")->AsBool(), "Transform.FindChild with skip beyond the last match must report found=false");

    const std::vector<kb::script::ScriptFunctionArgument> findMissingArgs{ parentEntityArg, kb::script::ScriptFunctionArgument{ "name", kb::script::ScriptValue{ std::string{ "NoSuchChild" } } } };
    const kb::script::ScriptFunctionCallResult findMissing = host.Functions().Call("Transform.FindChild", findMissingArgs, context);
    kb::tests::Require(findMissing.Succeeded() && !findMissing.Output("found")->AsBool(), "Transform.FindChild must report found=false for a name that does not match any child");

    // Dead entity: every function must fail cleanly, not throw.
    scene.Entities().Destroy(parentObject.Entity());
    const kb::script::ScriptFunctionCallResult deadChildCount = host.Functions().Call("Transform.ChildCount", parentOnlyArgs, context);
    kb::tests::Require(deadChildCount.Succeeded() && !deadChildCount.Output("found")->AsBool(), "Transform.ChildCount on a dead entity must report found=false, not throw");
    const kb::script::ScriptFunctionCallResult deadGetChild = host.Functions().Call("Transform.GetChild", getChild0Args, context);
    kb::tests::Require(deadGetChild.Succeeded() && !deadGetChild.Output("found")->AsBool(), "Transform.GetChild on a dead entity must report found=false, not throw");
    const kb::script::ScriptFunctionCallResult deadFindChild = host.Functions().Call("Transform.FindChild", findBarArgs, context);
    kb::tests::Require(deadFindChild.Succeeded() && !deadFindChild.Output("found")->AsBool(), "Transform.FindChild on a dead entity must report found=false, not throw");
}

// LIB-088: Transform.Rotate/LookAt/TransformPoint/InverseTransformPoint.
// Transform.Translate is deliberately NOT re-tested here — it already has
// coverage in RunScriptWorldTimePhysicsApiTest above and LIB-088 makes no
// change to it. Deliberately its own fresh Scene/host (isolated fixture
// pattern, LIB-067). Proves: Rotate genuinely COMPOSES (two calls produce
// the mathematically composed quaternion, not the last delta verbatim);
// LookAt on both a root AND a child of a non-trivially transformed parent
// produces a WORLD rotation whose forward vector actually points at the
// target (the real round-trip proof, mirroring LIB-085/086's own
// round-trip tests, not just "it compiles"); TransformPoint/
// InverseTransformPoint round-trip a point through both directions on a
// child of a translated+rotated+scaled parent.
void RunTransformApiRotateLookAtAndPointConversionTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Transform API rotate/lookAt/point conversion test host did not initialize");
    kb::tests::Require(host.Functions().FindSignature("Transform.Rotate") != nullptr, "Transform.Rotate was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.LookAt") != nullptr, "Transform.LookAt was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.TransformPoint") != nullptr, "Transform.TransformPoint was not registered");
    kb::tests::Require(host.Functions().FindSignature("Transform.InverseTransformPoint") != nullptr, "Transform.InverseTransformPoint was not registered");

    const kb::script::ScriptFunctionCallContext context{ .scene = &scene, .deltaSeconds = 0.016F };

    // (1) Rotate: two applications of the SAME 90-degree-around-Z delta
    // must genuinely COMPOSE to the mathematically expected 180-degree
    // result — not just apply the delta once, and not overwrite the
    // previous rotation with the delta verbatim.
    const kb::scene::SceneObject rotateSubject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "RotateSubject" });
    const kb::script::ScriptFunctionArgument rotateEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ rotateSubject.Entity().Id(), kb::script::ScriptValueType::Entity } };
    const kb::scene::Quat rotateDelta{ 0.0F, 0.0F, 0.7071068F, 0.7071068F }; // 90 degrees around +Z.
    const std::vector<kb::script::ScriptFunctionArgument> rotateArgs{
        rotateEntityArg,
        kb::script::ScriptFunctionArgument{ "x", kb::script::ScriptValue{ rotateDelta.x } },
        kb::script::ScriptFunctionArgument{ "y", kb::script::ScriptValue{ rotateDelta.y } },
        kb::script::ScriptFunctionArgument{ "z", kb::script::ScriptValue{ rotateDelta.z } },
        kb::script::ScriptFunctionArgument{ "w", kb::script::ScriptValue{ rotateDelta.w } },
    };
    const kb::script::ScriptFunctionCallResult firstRotate = host.Functions().Call("Transform.Rotate", rotateArgs, context);
    kb::tests::Require(firstRotate.Succeeded() && firstRotate.Output("moved")->AsBool(), "Transform.Rotate direct call failed");
    const kb::script::ScriptFunctionCallResult secondRotate = host.Functions().Call("Transform.Rotate", rotateArgs, context);
    kb::tests::Require(secondRotate.Succeeded() && secondRotate.Output("moved")->AsBool(), "Transform.Rotate second direct call failed");
    const kb::scene::Quat expectedComposedRotation = kb::math::Normalize(rotateDelta * rotateDelta);
    const kb::scene::Quat actualComposedRotation = scene.Transforms().Get(rotateSubject.Entity()).localRotation;
    kb::tests::Require(kb::tests::NearlyEqual(std::fabs(actualComposedRotation.z), std::fabs(expectedComposedRotation.z))
            && kb::tests::NearlyEqual(std::fabs(actualComposedRotation.w), std::fabs(expectedComposedRotation.w)),
        "Transform.Rotate must genuinely COMPOSE two deltas (local = local * delta), not overwrite with the delta verbatim");

    // (2) LookAt on a ROOT entity: the resulting rotation's forward vector
    // (local +Z rotated by the result) must actually point toward the
    // requested world-space target.
    const kb::scene::SceneObject lookAtRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "LookAtRoot" });
    const kb::script::ScriptFunctionArgument lookAtRootEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ lookAtRoot.Entity().Id(), kb::script::ScriptValueType::Entity } };
    const std::vector<kb::script::ScriptFunctionArgument> lookAtRootArgs{
        lookAtRootEntityArg,
        kb::script::ScriptFunctionArgument{ "targetX", kb::script::ScriptValue{ 5.0F } },
        kb::script::ScriptFunctionArgument{ "targetY", kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ "targetZ", kb::script::ScriptValue{ 0.0F } },
    };
    const kb::script::ScriptFunctionCallResult lookAtRootResult = host.Functions().Call("Transform.LookAt", lookAtRootArgs, context);
    kb::tests::Require(lookAtRootResult.Succeeded() && lookAtRootResult.Output("moved")->AsBool(), "Transform.LookAt (root) direct call failed");
    const kb::scene::Quat rootLocalRotationAfterLookAt = scene.Transforms().Get(lookAtRoot.Entity()).localRotation;
    const kb::math::Vec3 rootForwardAfterLookAt = kb::math::Rotate(rootLocalRotationAfterLookAt, kb::math::Vec3{ 0.0F, 0.0F, 1.0F });
    kb::tests::Require(kb::tests::NearlyEqual(rootForwardAfterLookAt.x, 1.0F) && std::fabs(rootForwardAfterLookAt.y) < 0.001F && std::fabs(rootForwardAfterLookAt.z) < 0.001F,
        "Transform.LookAt (root) must produce a rotation whose forward vector points toward the requested target");

    // (3) LookAt on a CHILD of a non-trivially transformed (translated AND
    // rotated) parent: the resulting WORLD rotation's forward vector must
    // STILL point at the target — proving the back-solve through
    // WorldPoseToLocal correctly undoes the parent's own rotation.
    const kb::scene::SceneObject lookAtParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "LookAtParent" });
    kb::scene::TransformComponent lookAtParentTransform = scene.Transforms().Get(lookAtParent.Entity());
    lookAtParentTransform.localPosition = kb::scene::Vec3{ 10.0F, 0.0F, 0.0F };
    lookAtParentTransform.localRotation = kb::scene::Quat{ 0.0F, 0.7071068F, 0.0F, 0.7071068F }; // 90 degrees around +Y.
    scene.Transforms().Set(lookAtParent.Entity(), lookAtParentTransform);
    const kb::scene::SceneObject lookAtChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "LookAtChild" });
    kb::tests::Require(scene.Hierarchy().SetParent(lookAtChild.Entity(), lookAtParent.Entity()), "LookAt child fixture could not be parented");
    static_cast<void>(scene.Runtime().Update(0.0F));
    const kb::scene::Vec3 lookAtChildWorldPositionBefore = scene.Transforms().Get(lookAtChild.Entity()).worldPosition;
    const kb::script::ScriptFunctionArgument lookAtChildEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ lookAtChild.Entity().Id(), kb::script::ScriptValueType::Entity } };
    const std::vector<kb::script::ScriptFunctionArgument> lookAtChildArgs{
        lookAtChildEntityArg,
        kb::script::ScriptFunctionArgument{ "targetX", kb::script::ScriptValue{ lookAtChildWorldPositionBefore.x } },
        kb::script::ScriptFunctionArgument{ "targetY", kb::script::ScriptValue{ lookAtChildWorldPositionBefore.y + 5.0F } },
        kb::script::ScriptFunctionArgument{ "targetZ", kb::script::ScriptValue{ lookAtChildWorldPositionBefore.z } },
    };
    const kb::script::ScriptFunctionCallResult lookAtChildResult = host.Functions().Call("Transform.LookAt", lookAtChildArgs, context);
    kb::tests::Require(lookAtChildResult.Succeeded() && lookAtChildResult.Output("moved")->AsBool(), "Transform.LookAt (child) direct call failed");
    const kb::scene::TransformComponent lookAtChildTransformAfter = scene.Transforms().Get(lookAtChild.Entity());
    kb::tests::Require(kb::tests::NearlyEqual(lookAtChildTransformAfter.worldPosition.x, lookAtChildWorldPositionBefore.x)
            && kb::tests::NearlyEqual(lookAtChildTransformAfter.worldPosition.y, lookAtChildWorldPositionBefore.y)
            && kb::tests::NearlyEqual(lookAtChildTransformAfter.worldPosition.z, lookAtChildWorldPositionBefore.z),
        "Transform.LookAt must not move the entity — only its rotation changes");
    const kb::math::Vec3 childForwardAfterLookAt = kb::math::Rotate(lookAtChildTransformAfter.worldRotation, kb::math::Vec3{ 0.0F, 0.0F, 1.0F });
    kb::tests::Require(std::fabs(childForwardAfterLookAt.x) < 0.001F && kb::tests::NearlyEqual(childForwardAfterLookAt.y, 1.0F) && std::fabs(childForwardAfterLookAt.z) < 0.001F,
        "Transform.LookAt on a child of a rotated parent must still produce a WORLD forward vector pointing at the target — the back-solve must undo the parent's own rotation");

    // (4) TransformPoint/InverseTransformPoint round trip on a child of a
    // translated+rotated+scaled parent.
    const kb::scene::SceneObject pointParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "PointConversionParent" });
    kb::scene::TransformComponent pointParentTransform = scene.Transforms().Get(pointParent.Entity());
    pointParentTransform.localPosition = kb::scene::Vec3{ 3.0F, 4.0F, 5.0F };
    pointParentTransform.localRotation = kb::scene::Quat{ 0.0F, 0.0F, 0.7071068F, 0.7071068F }; // 90 degrees around +Z.
    pointParentTransform.localScale = kb::scene::Vec3{ 2.0F, 2.0F, 2.0F };
    scene.Transforms().Set(pointParent.Entity(), pointParentTransform);
    const kb::scene::SceneObject pointChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "PointConversionChild" });
    kb::tests::Require(scene.Hierarchy().SetParent(pointChild.Entity(), pointParent.Entity()), "Point conversion child fixture could not be parented");
    kb::scene::TransformComponent pointChildTransform = scene.Transforms().Get(pointChild.Entity());
    pointChildTransform.localPosition = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F };
    pointChildTransform.localRotation = kb::scene::Quat{ 0.0F, 0.7071068F, 0.0F, 0.7071068F }; // 90 degrees around +Y.
    scene.Transforms().Set(pointChild.Entity(), pointChildTransform);
    static_cast<void>(scene.Runtime().Update(0.0F));

    const kb::script::ScriptFunctionArgument pointChildEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ pointChild.Entity().Id(), kb::script::ScriptValueType::Entity } };
    const std::vector<kb::script::ScriptFunctionArgument> transformPointArgs{
        pointChildEntityArg,
        kb::script::ScriptFunctionArgument{ "x", kb::script::ScriptValue{ 2.0F } },
        kb::script::ScriptFunctionArgument{ "y", kb::script::ScriptValue{ 3.0F } },
        kb::script::ScriptFunctionArgument{ "z", kb::script::ScriptValue{ 4.0F } },
    };
    const kb::script::ScriptFunctionCallResult transformPointResult = host.Functions().Call("Transform.TransformPoint", transformPointArgs, context);
    kb::tests::Require(transformPointResult.Succeeded() && transformPointResult.Output("found")->AsBool(), "Transform.TransformPoint direct call failed");

    const std::vector<kb::script::ScriptFunctionArgument> inverseTransformPointArgs{
        pointChildEntityArg,
        kb::script::ScriptFunctionArgument{ "x", kb::script::ScriptValue{ transformPointResult.Output("x")->AsFloat() } },
        kb::script::ScriptFunctionArgument{ "y", kb::script::ScriptValue{ transformPointResult.Output("y")->AsFloat() } },
        kb::script::ScriptFunctionArgument{ "z", kb::script::ScriptValue{ transformPointResult.Output("z")->AsFloat() } },
    };
    const kb::script::ScriptFunctionCallResult inverseTransformPointResult = host.Functions().Call("Transform.InverseTransformPoint", inverseTransformPointArgs, context);
    kb::tests::Require(inverseTransformPointResult.Succeeded() && inverseTransformPointResult.Output("found")->AsBool()
            && kb::tests::NearlyEqual(inverseTransformPointResult.Output("x")->AsFloat(), 2.0F)
            && kb::tests::NearlyEqual(inverseTransformPointResult.Output("y")->AsFloat(), 3.0F)
            && kb::tests::NearlyEqual(inverseTransformPointResult.Output("z")->AsFloat(), 4.0F),
        "Transform.TransformPoint followed by Transform.InverseTransformPoint must round-trip the exact original local point, through a translated+rotated+scaled parent");

    // Also prove TransformPoint actually did something non-trivial — the
    // world point must differ from the input local point, given the
    // non-identity parent chain.
    kb::tests::Require(!kb::tests::NearlyEqual(transformPointResult.Output("x")->AsFloat(), 2.0F)
            || !kb::tests::NearlyEqual(transformPointResult.Output("y")->AsFloat(), 3.0F)
            || !kb::tests::NearlyEqual(transformPointResult.Output("z")->AsFloat(), 4.0F),
        "Transform.TransformPoint must produce a genuinely different WORLD point for a non-trivial parent chain, not just echo the local input");

    // Dead entity: every function must fail cleanly, not throw.
    scene.Entities().Destroy(rotateSubject.Entity());
    const kb::script::ScriptFunctionCallResult deadRotate = host.Functions().Call("Transform.Rotate", rotateArgs, context);
    kb::tests::Require(deadRotate.Succeeded() && !deadRotate.Output("moved")->AsBool(), "Transform.Rotate on a dead entity must report moved=false, not throw");

    const std::vector<kb::script::ScriptFunctionArgument> deadLookAtArgs{
        rotateEntityArg,
        kb::script::ScriptFunctionArgument{ "targetX", kb::script::ScriptValue{ 1.0F } },
        kb::script::ScriptFunctionArgument{ "targetY", kb::script::ScriptValue{ 0.0F } },
        kb::script::ScriptFunctionArgument{ "targetZ", kb::script::ScriptValue{ 0.0F } },
    };
    const kb::script::ScriptFunctionCallResult deadLookAt = host.Functions().Call("Transform.LookAt", deadLookAtArgs, context);
    kb::tests::Require(deadLookAt.Succeeded() && !deadLookAt.Output("moved")->AsBool(), "Transform.LookAt on a dead entity must report moved=false, not throw");

    const std::vector<kb::script::ScriptFunctionArgument> deadTransformPointArgs{
        rotateEntityArg,
        kb::script::ScriptFunctionArgument{ "x", kb::script::ScriptValue{ 1.0F } },
        kb::script::ScriptFunctionArgument{ "y", kb::script::ScriptValue{ 1.0F } },
        kb::script::ScriptFunctionArgument{ "z", kb::script::ScriptValue{ 1.0F } },
    };
    const kb::script::ScriptFunctionCallResult deadTransformPoint = host.Functions().Call("Transform.TransformPoint", deadTransformPointArgs, context);
    kb::tests::Require(deadTransformPoint.Succeeded() && !deadTransformPoint.Output("found")->AsBool(), "Transform.TransformPoint on a dead entity must report found=false, not throw");
    const kb::script::ScriptFunctionCallResult deadInverseTransformPoint = host.Functions().Call("Transform.InverseTransformPoint", deadTransformPointArgs, context);
    kb::tests::Require(deadInverseTransformPoint.Succeeded() && !deadInverseTransformPoint.Output("found")->AsBool(), "Transform.InverseTransformPoint on a dead entity must report found=false, not throw");
}

// LIB-091: test-only — closes 4 real coverage gaps left by LIB-085/086/088's
// own tests (all 2-3 level fixtures, uniform positive scale only), per
// research confirming NO existing bug, just untested scenarios. Each
// scenario gets its own isolated Scene/host (LIB-067 pattern).
void RunTransformHierarchyEdgeCaseTest() {
    // (1a) keepWorld with a NON-UNIFORM parent scale — proves the
    // world-to-local back-solve (WorldPoseToLocal's per-axis SafeDivide) is
    // correct for (2,3,4), not just LIB-086's uniform 2x fixture.
    {
        kb::scene::Scene scene;
        kb::script::ScriptRuntimeHost host{ scene };
        kb::tests::Require(host.Succeeded(), "LIB-091 keepWorld non-uniform-scale test host did not initialize");
        const kb::script::ScriptFunctionCallContext context{ .scene = &scene, .deltaSeconds = 0.016F };

        const kb::scene::SceneObject newParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "KeepWorldNonUniformParent" });
        kb::scene::TransformComponent newParentTransform = scene.Transforms().Get(newParent.Entity());
        newParentTransform.localPosition = kb::scene::Vec3{ 20.0F, 0.0F, 0.0F };
        newParentTransform.localScale = kb::scene::Vec3{ 2.0F, 3.0F, 4.0F }; // Non-uniform.
        scene.Transforms().Set(newParent.Entity(), newParentTransform);

        const kb::scene::SceneObject reparentedEntity = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "KeepWorldNonUniformEntity" });
        kb::scene::TransformComponent reparentedTransform = scene.Transforms().Get(reparentedEntity.Entity());
        reparentedTransform.localPosition = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F };
        scene.Transforms().Set(reparentedEntity.Entity(), reparentedTransform);
        static_cast<void>(scene.Runtime().Update(0.0F));
        const kb::scene::Vec3 entityWorldPosBefore = scene.Transforms().Get(reparentedEntity.Entity()).worldPosition;

        const std::vector<kb::script::ScriptFunctionArgument> keepWorldArgs{
            kb::script::ScriptFunctionArgument{ "entity", kb::script::ScriptValue{ reparentedEntity.Entity().Id(), kb::script::ScriptValueType::Entity } },
            kb::script::ScriptFunctionArgument{ "parent", kb::script::ScriptValue{ newParent.Entity().Id(), kb::script::ScriptValueType::Entity } },
            kb::script::ScriptFunctionArgument{ "keepWorld", kb::script::ScriptValue{ true } },
        };
        const kb::script::ScriptFunctionCallResult keepWorldResult = host.Functions().Call("Transform.SetParent", keepWorldArgs, context);
        kb::tests::Require(keepWorldResult.Succeeded() && keepWorldResult.Output("moved")->AsBool(), "LIB-091 Transform.SetParent(keepWorld=true) under a non-uniform-scale parent failed");

        const kb::scene::Vec3 entityWorldPosAfter = scene.Transforms().Get(reparentedEntity.Entity()).worldPosition;
        kb::tests::Require(kb::tests::NearlyEqual(entityWorldPosAfter.x, entityWorldPosBefore.x) && kb::tests::NearlyEqual(entityWorldPosAfter.y, entityWorldPosBefore.y) && kb::tests::NearlyEqual(entityWorldPosAfter.z, entityWorldPosBefore.z),
            "LIB-091 keepWorld must preserve the exact world position under a NON-UNIFORM (2,3,4) parent scale, not just uniform scale");
    }

    // (1b) keepWorld reparenting an entity that itself has a child — proves
    // subtree consistency IN THE CASE keepWorld ACTUALLY GUARANTEES IT: a
    // reparent that does NOT change the entity's effective inherited world
    // scale (both the old and new parent are unit-scale here). keepWorld's
    // own implementation (ScriptTransformApi.cpp's SetParent) ONLY
    // back-solves the DIRECTLY reparented entity's own local pose — it
    // never touches a descendant's local transform (the same well-known
    // limitation Unity's own Transform.SetParent(worldPositionStays) has).
    // A first version of this test wrongly asserted subtree preservation
    // across a SCALE-CHANGING reparent (case 1a's non-uniform-scale
    // parent) and correctly failed — that is not a bug, it is inherent to
    // "only the reparented entity's own pose is preserved," so this test
    // deliberately keeps scale uniform/unchanged to isolate and prove the
    // guarantee keepWorld actually makes.
    {
        kb::scene::Scene scene;
        kb::script::ScriptRuntimeHost host{ scene };
        kb::tests::Require(host.Succeeded(), "LIB-091 keepWorld subtree-consistency test host did not initialize");
        const kb::script::ScriptFunctionCallContext context{ .scene = &scene, .deltaSeconds = 0.016F };

        const kb::scene::SceneObject newParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "KeepWorldSubtreeParent" });
        kb::scene::TransformComponent newParentTransform = scene.Transforms().Get(newParent.Entity());
        newParentTransform.localPosition = kb::scene::Vec3{ 20.0F, 0.0F, 0.0F }; // Unit scale (default).
        scene.Transforms().Set(newParent.Entity(), newParentTransform);

        const kb::scene::SceneObject reparentedEntity = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "KeepWorldSubtreeEntity" });
        kb::scene::TransformComponent reparentedTransform = scene.Transforms().Get(reparentedEntity.Entity());
        reparentedTransform.localPosition = kb::scene::Vec3{ 1.0F, 2.0F, 3.0F }; // Also unit scale — no scale change across the reparent.
        scene.Transforms().Set(reparentedEntity.Entity(), reparentedTransform);

        const kb::scene::SceneObject subtreeChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "KeepWorldSubtreeChild" });
        kb::tests::Require(scene.Hierarchy().SetParent(subtreeChild.Entity(), reparentedEntity.Entity()), "LIB-091 keepWorld subtree fixture could not attach the subtree child");
        kb::scene::TransformComponent subtreeChildTransform = scene.Transforms().Get(subtreeChild.Entity());
        subtreeChildTransform.localPosition = kb::scene::Vec3{ 0.5F, 0.5F, 0.5F };
        scene.Transforms().Set(subtreeChild.Entity(), subtreeChildTransform);

        static_cast<void>(scene.Runtime().Update(0.0F));
        const kb::scene::Vec3 subtreeChildWorldPosBefore = scene.Transforms().Get(subtreeChild.Entity()).worldPosition;

        const std::vector<kb::script::ScriptFunctionArgument> keepWorldArgs{
            kb::script::ScriptFunctionArgument{ "entity", kb::script::ScriptValue{ reparentedEntity.Entity().Id(), kb::script::ScriptValueType::Entity } },
            kb::script::ScriptFunctionArgument{ "parent", kb::script::ScriptValue{ newParent.Entity().Id(), kb::script::ScriptValueType::Entity } },
            kb::script::ScriptFunctionArgument{ "keepWorld", kb::script::ScriptValue{ true } },
        };
        const kb::script::ScriptFunctionCallResult keepWorldResult = host.Functions().Call("Transform.SetParent", keepWorldArgs, context);
        kb::tests::Require(keepWorldResult.Succeeded() && keepWorldResult.Output("moved")->AsBool(), "LIB-091 Transform.SetParent(keepWorld=true) subtree fixture reparent failed");

        const kb::scene::Vec3 subtreeChildWorldPosAfter = scene.Transforms().Get(subtreeChild.Entity()).worldPosition;
        kb::tests::Require(kb::tests::NearlyEqual(subtreeChildWorldPosAfter.x, subtreeChildWorldPosBefore.x) && kb::tests::NearlyEqual(subtreeChildWorldPosAfter.y, subtreeChildWorldPosBefore.y) && kb::tests::NearlyEqual(subtreeChildWorldPosAfter.z, subtreeChildWorldPosBefore.z),
            "LIB-091 keepWorld must preserve a subtree child's world pose when the reparent does not change the reparented entity's effective inherited scale");
    }

    // (2) Parent destroy: cascading destroy of a middle entity in a 3-level
    // chain must be reflected honestly by Transform.Parent/WorldPose (on
    // the now-destroyed child) and Transform.ChildCount (on the surviving
    // grandparent, which must NOT report a stale child count).
    {
        kb::scene::Scene scene;
        kb::script::ScriptRuntimeHost host{ scene };
        kb::tests::Require(host.Succeeded(), "LIB-091 parent-destroy test host did not initialize");
        const kb::script::ScriptFunctionCallContext context{ .scene = &scene, .deltaSeconds = 0.016F };

        const kb::scene::SceneObject grandparent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "DestroyChainGrandparent" });
        const kb::scene::SceneObject middleParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "DestroyChainMiddleParent" });
        const kb::scene::SceneObject leafChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "DestroyChainLeafChild" });
        kb::tests::Require(scene.Hierarchy().SetParent(middleParent.Entity(), grandparent.Entity()), "LIB-091 destroy fixture could not attach middleParent");
        kb::tests::Require(scene.Hierarchy().SetParent(leafChild.Entity(), middleParent.Entity()), "LIB-091 destroy fixture could not attach leafChild");
        static_cast<void>(scene.Runtime().Update(0.0F));

        const kb::script::ScriptFunctionArgument grandparentEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ grandparent.Entity().Id(), kb::script::ScriptValueType::Entity } };
        const std::vector<kb::script::ScriptFunctionArgument> grandparentOnlyArgs{ grandparentEntityArg };
        const kb::script::ScriptFunctionCallResult childCountBeforeDestroy = host.Functions().Call("Transform.ChildCount", grandparentOnlyArgs, context);
        kb::tests::Require(childCountBeforeDestroy.Succeeded() && childCountBeforeDestroy.Output("count")->AsInt() == 1, "LIB-091 destroy fixture setup: grandparent must have exactly 1 child before the destroy");

        scene.Entities().Destroy(middleParent.Entity()); // Cascades to leafChild (kb::scene::SceneEntityDestructionService).
        kb::tests::Require(!scene.Entities().IsAlive(leafChild.Entity()), "LIB-091 destroying middleParent must cascade-destroy leafChild");

        const kb::script::ScriptFunctionArgument leafChildEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ leafChild.Entity().Id(), kb::script::ScriptValueType::Entity } };
        const std::vector<kb::script::ScriptFunctionArgument> leafChildOnlyArgs{ leafChildEntityArg };
        const kb::script::ScriptFunctionCallResult parentAfterDestroy = host.Functions().Call("Transform.Parent", leafChildOnlyArgs, context);
        kb::tests::Require(parentAfterDestroy.Succeeded() && !parentAfterDestroy.Output("found")->AsBool(), "LIB-091 Transform.Parent on a cascade-destroyed child must report found=false, not throw or report a stale parent");
        const kb::script::ScriptFunctionCallResult worldPoseAfterDestroy = host.Functions().Call("Transform.WorldPose", leafChildOnlyArgs, context);
        kb::tests::Require(worldPoseAfterDestroy.Succeeded() && !worldPoseAfterDestroy.Output("found")->AsBool(), "LIB-091 Transform.WorldPose on a cascade-destroyed child must report found=false, not throw or report a stale pose");

        const kb::script::ScriptFunctionCallResult childCountAfterDestroy = host.Functions().Call("Transform.ChildCount", grandparentOnlyArgs, context);
        kb::tests::Require(childCountAfterDestroy.Succeeded() && childCountAfterDestroy.Output("found")->AsBool() && childCountAfterDestroy.Output("count")->AsInt() == 0,
            "LIB-091 Transform.ChildCount on the surviving grandparent must drop to 0 after its only child (middleParent) is destroyed, not report a stale count");
    }

    // (3) Deep hierarchy: a 30-level chain, each level offset by (1,0,0) in
    // local space with no rotation, so the leaf's expected world X is
    // exactly the chain depth — proves SceneTransformHierarchySystem's
    // iterative (not recursive) propagation produces a numerically correct
    // result at real depth, not just "doesn't crash."
    {
        kb::scene::Scene scene;
        kb::script::ScriptRuntimeHost host{ scene };
        kb::tests::Require(host.Succeeded(), "LIB-091 deep-hierarchy test host did not initialize");
        const kb::script::ScriptFunctionCallContext context{ .scene = &scene, .deltaSeconds = 0.016F };

        constexpr int kChainDepth = 30;
        kb::scene::SceneEntity previous{};
        kb::scene::SceneEntity leaf{};
        for (int level = 0; level < kChainDepth; ++level) {
            const kb::scene::SceneObject node = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "DeepChainNode" });
            if (previous.IsValid()) {
                kb::tests::Require(scene.Hierarchy().SetParent(node.Entity(), previous), "LIB-091 deep-hierarchy fixture could not extend the chain");
            }
            kb::scene::TransformComponent nodeTransform = scene.Transforms().Get(node.Entity());
            nodeTransform.localPosition = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F };
            scene.Transforms().Set(node.Entity(), nodeTransform);
            previous = node.Entity();
            leaf = node.Entity();
        }
        static_cast<void>(scene.Runtime().Update(0.0F));

        const kb::script::ScriptFunctionArgument leafEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ leaf.Id(), kb::script::ScriptValueType::Entity } };
        const std::vector<kb::script::ScriptFunctionArgument> leafOnlyArgs{ leafEntityArg };
        const kb::script::ScriptFunctionCallResult leafWorldPose = host.Functions().Call("Transform.WorldPose", leafOnlyArgs, context);
        kb::tests::Require(leafWorldPose.Succeeded() && leafWorldPose.Output("found")->AsBool() && kb::tests::NearlyEqual(leafWorldPose.Output("posX")->AsFloat(), static_cast<float>(kChainDepth)),
            "LIB-091 a 30-level deep hierarchy chain must propagate to a numerically correct leaf world position, not just avoid crashing");
    }

    // (4) Zero/negative scale — the highest-risk, never-before-exercised
    // scenario: SafeDivide (ScriptTransformApi.cpp) exists precisely for a
    // near-zero parent scale axis, and TransformMath's uniform-scale FAST
    // PATH (CanUseUniformScaleParentFastPath) is reachable by a NEGATIVE
    // uniform scale too (it only compares axes for equality, not sign) —
    // neither has ever been exercised by a test before this.
    {
        kb::scene::Scene scene;
        kb::script::ScriptRuntimeHost host{ scene };
        kb::tests::Require(host.Succeeded(), "LIB-091 zero/negative-scale test host did not initialize");
        const kb::script::ScriptFunctionCallContext context{ .scene = &scene, .deltaSeconds = 0.016F };

        // (4a) Near-zero parent scale axis: SetWorldPose must not produce
        // NaN/Inf, and must still honestly report moved=true.
        const kb::scene::SceneObject zeroScaleParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ZeroScaleParent" });
        kb::scene::TransformComponent zeroScaleParentTransform = scene.Transforms().Get(zeroScaleParent.Entity());
        zeroScaleParentTransform.localPosition = kb::scene::Vec3{ 5.0F, 0.0F, 0.0F };
        zeroScaleParentTransform.localScale = kb::scene::Vec3{ 0.0F, 1.0F, 1.0F }; // X axis genuinely zero.
        scene.Transforms().Set(zeroScaleParent.Entity(), zeroScaleParentTransform);
        const kb::scene::SceneObject zeroScaleChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "ZeroScaleChild" });
        kb::tests::Require(scene.Hierarchy().SetParent(zeroScaleChild.Entity(), zeroScaleParent.Entity()), "LIB-091 zero-scale fixture could not attach the child");
        static_cast<void>(scene.Runtime().Update(0.0F));

        const std::vector<kb::script::ScriptFunctionArgument> zeroScaleSetWorldPoseArgs{
            kb::script::ScriptFunctionArgument{ "entity", kb::script::ScriptValue{ zeroScaleChild.Entity().Id(), kb::script::ScriptValueType::Entity } },
            kb::script::ScriptFunctionArgument{ "posX", kb::script::ScriptValue{ 10.0F } },
            kb::script::ScriptFunctionArgument{ "posY", kb::script::ScriptValue{ 5.0F } },
            kb::script::ScriptFunctionArgument{ "posZ", kb::script::ScriptValue{ 5.0F } },
            kb::script::ScriptFunctionArgument{ "rotX", kb::script::ScriptValue{ 0.0F } },
            kb::script::ScriptFunctionArgument{ "rotY", kb::script::ScriptValue{ 0.0F } },
            kb::script::ScriptFunctionArgument{ "rotZ", kb::script::ScriptValue{ 0.0F } },
            kb::script::ScriptFunctionArgument{ "rotW", kb::script::ScriptValue{ 1.0F } },
        };
        const kb::script::ScriptFunctionCallResult zeroScaleSetWorldPoseResult = host.Functions().Call("Transform.SetWorldPose", zeroScaleSetWorldPoseArgs, context);
        kb::tests::Require(zeroScaleSetWorldPoseResult.Succeeded() && zeroScaleSetWorldPoseResult.Output("moved")->AsBool(), "LIB-091 Transform.SetWorldPose under a zero-scale parent axis must still honestly report moved=true, not fail");
        const kb::scene::TransformComponent zeroScaleChildTransform = scene.Transforms().Get(zeroScaleChild.Entity());
        kb::tests::Require(std::isfinite(zeroScaleChildTransform.localPosition.x) && std::isfinite(zeroScaleChildTransform.localPosition.y) && std::isfinite(zeroScaleChildTransform.localPosition.z)
                && std::isfinite(zeroScaleChildTransform.localRotation.x) && std::isfinite(zeroScaleChildTransform.localRotation.y) && std::isfinite(zeroScaleChildTransform.localRotation.z) && std::isfinite(zeroScaleChildTransform.localRotation.w),
            "LIB-091 SafeDivide must prevent a zero parent scale axis from producing NaN/Inf in the back-solved local pose");

        // (4b) Negative UNIFORM parent scale (a mirror) — reachable through
        // TransformMath's fast path, not just the general Compose path.
        // TransformPoint/InverseTransformPoint and SetWorldPose/WorldPose
        // must both round-trip exactly.
        const kb::scene::SceneObject negativeScaleParent = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "NegativeScaleParent" });
        kb::scene::TransformComponent negativeScaleParentTransform = scene.Transforms().Get(negativeScaleParent.Entity());
        negativeScaleParentTransform.localPosition = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F };
        negativeScaleParentTransform.localScale = kb::scene::Vec3{ -2.0F, -2.0F, -2.0F }; // Uniform negative — mirror + scale.
        scene.Transforms().Set(negativeScaleParent.Entity(), negativeScaleParentTransform);
        const kb::scene::SceneObject negativeScaleChild = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "NegativeScaleChild" });
        kb::tests::Require(scene.Hierarchy().SetParent(negativeScaleChild.Entity(), negativeScaleParent.Entity()), "LIB-091 negative-scale fixture could not attach the child");
        static_cast<void>(scene.Runtime().Update(0.0F));

        const kb::script::ScriptFunctionArgument negativeScaleChildEntityArg{ .name = "entity", .value = kb::script::ScriptValue{ negativeScaleChild.Entity().Id(), kb::script::ScriptValueType::Entity } };
        const std::vector<kb::script::ScriptFunctionArgument> negativeScaleTransformPointArgs{
            negativeScaleChildEntityArg,
            kb::script::ScriptFunctionArgument{ "x", kb::script::ScriptValue{ 3.0F } },
            kb::script::ScriptFunctionArgument{ "y", kb::script::ScriptValue{ 4.0F } },
            kb::script::ScriptFunctionArgument{ "z", kb::script::ScriptValue{ 5.0F } },
        };
        const kb::script::ScriptFunctionCallResult negativeScaleTransformPointResult = host.Functions().Call("Transform.TransformPoint", negativeScaleTransformPointArgs, context);
        kb::tests::Require(negativeScaleTransformPointResult.Succeeded() && negativeScaleTransformPointResult.Output("found")->AsBool(), "LIB-091 Transform.TransformPoint under a negative-uniform-scale parent failed");
        const std::vector<kb::script::ScriptFunctionArgument> negativeScaleInverseTransformPointArgs{
            negativeScaleChildEntityArg,
            kb::script::ScriptFunctionArgument{ "x", kb::script::ScriptValue{ negativeScaleTransformPointResult.Output("x")->AsFloat() } },
            kb::script::ScriptFunctionArgument{ "y", kb::script::ScriptValue{ negativeScaleTransformPointResult.Output("y")->AsFloat() } },
            kb::script::ScriptFunctionArgument{ "z", kb::script::ScriptValue{ negativeScaleTransformPointResult.Output("z")->AsFloat() } },
        };
        const kb::script::ScriptFunctionCallResult negativeScaleInverseTransformPointResult = host.Functions().Call("Transform.InverseTransformPoint", negativeScaleInverseTransformPointArgs, context);
        kb::tests::Require(negativeScaleInverseTransformPointResult.Succeeded() && negativeScaleInverseTransformPointResult.Output("found")->AsBool()
                && kb::tests::NearlyEqual(negativeScaleInverseTransformPointResult.Output("x")->AsFloat(), 3.0F)
                && kb::tests::NearlyEqual(negativeScaleInverseTransformPointResult.Output("y")->AsFloat(), 4.0F)
                && kb::tests::NearlyEqual(negativeScaleInverseTransformPointResult.Output("z")->AsFloat(), 5.0F),
            "LIB-091 TransformPoint followed by InverseTransformPoint must round-trip exactly through a NEGATIVE uniform parent scale (a mirror), reachable via TransformMath's uniform-scale fast path");

        const std::vector<kb::script::ScriptFunctionArgument> negativeScaleSetWorldPoseArgs{
            negativeScaleChildEntityArg,
            kb::script::ScriptFunctionArgument{ "posX", kb::script::ScriptValue{ 7.0F } },
            kb::script::ScriptFunctionArgument{ "posY", kb::script::ScriptValue{ 8.0F } },
            kb::script::ScriptFunctionArgument{ "posZ", kb::script::ScriptValue{ 9.0F } },
            kb::script::ScriptFunctionArgument{ "rotX", kb::script::ScriptValue{ 0.0F } },
            kb::script::ScriptFunctionArgument{ "rotY", kb::script::ScriptValue{ 0.0F } },
            kb::script::ScriptFunctionArgument{ "rotZ", kb::script::ScriptValue{ 0.0F } },
            kb::script::ScriptFunctionArgument{ "rotW", kb::script::ScriptValue{ 1.0F } },
        };
        const kb::script::ScriptFunctionCallResult negativeScaleSetWorldPoseResult = host.Functions().Call("Transform.SetWorldPose", negativeScaleSetWorldPoseArgs, context);
        kb::tests::Require(negativeScaleSetWorldPoseResult.Succeeded() && negativeScaleSetWorldPoseResult.Output("moved")->AsBool(), "LIB-091 Transform.SetWorldPose under a negative-uniform-scale parent failed");
        const std::vector<kb::script::ScriptFunctionArgument> negativeScaleChildOnlyArgs{ negativeScaleChildEntityArg };
        const kb::script::ScriptFunctionCallResult negativeScaleWorldPoseResult = host.Functions().Call("Transform.WorldPose", negativeScaleChildOnlyArgs, context);
        kb::tests::Require(negativeScaleWorldPoseResult.Succeeded() && negativeScaleWorldPoseResult.Output("found")->AsBool()
                && kb::tests::NearlyEqual(negativeScaleWorldPoseResult.Output("posX")->AsFloat(), 7.0F)
                && kb::tests::NearlyEqual(negativeScaleWorldPoseResult.Output("posY")->AsFloat(), 8.0F)
                && kb::tests::NearlyEqual(negativeScaleWorldPoseResult.Output("posZ")->AsFloat(), 9.0F),
            "LIB-091 SetWorldPose followed by WorldPose must round-trip the exact requested world pose through a NEGATIVE uniform parent scale");
    }
}

// LIB-067: World.Destroy idempotency (repeat call on an already-dead
// entity is a safe no-op, not an error) and the "deferred" flag being
// HONEST about this engine's current immediate-only lifecycle (rejected
// with a clear error rather than silently behaving as immediate or
// crashing later — see the design note on ScriptWorldApi.cpp's Destroy).
// Deliberately its OWN fresh Scene/host rather than reusing
// RunScriptWorldTimePhysicsApiTest's — a create-then-immediately-destroy
// cycle interleaved into that giant, order-sensitive integration test was
// observed to make an UNRELATED, LATER Physics.Raycast in the same test
// hit the wrong entity (very likely stale broadphase/index-reuse state
// from the freed entity slot, not anything about Destroy's own
// correctness) — a real but separate finding, noted in _temp.md, that a
// small isolated test sidesteps rather than chases down here.
void RunWorldDestroyDeferredFlagTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "World.Destroy deferred-flag test host setup failed");

    const kb::scene::SceneEntity target = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "DestroyTarget" });
    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };
    const std::vector<kb::script::ScriptFunctionArgument> destroyArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ target.Id(), kb::script::ScriptValueType::Entity } },
    };
    const kb::script::ScriptFunctionCallResult firstDestroy = host.Functions().Call("World.Destroy", destroyArgs, context);
    kb::tests::Require(firstDestroy.Succeeded() && firstDestroy.Output("destroyed")->AsBool(), "World.Destroy must report destroyed=true for a live entity");
    kb::tests::Require(!scene.Entities().IsAlive(target), "World.Destroy did not actually destroy the entity");

    const kb::script::ScriptFunctionCallResult secondDestroy = host.Functions().Call("World.Destroy", destroyArgs, context);
    kb::tests::Require(secondDestroy.Succeeded() && !secondDestroy.Output("destroyed")->AsBool(),
        "World.Destroy must be idempotent: a repeat call on an already-dead entity must succeed with destroyed=false, not error");
    const kb::script::ScriptFunctionCallResult thirdDestroy = host.Functions().Call("World.Destroy", destroyArgs, context);
    kb::tests::Require(thirdDestroy.Succeeded() && !thirdDestroy.Output("destroyed")->AsBool(), "World.Destroy must remain idempotent across more than two repeat calls");

    const kb::scene::SceneEntity liveEntity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "StillAlive" });
    const std::vector<kb::script::ScriptFunctionArgument> deferredDestroyArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ liveEntity.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "deferred", .value = kb::script::ScriptValue{ true } },
    };
    const kb::script::ScriptFunctionCallResult deferredDestroy = host.Functions().Call("World.Destroy", deferredDestroyArgs, context);
    kb::tests::Require(!deferredDestroy.Succeeded(), "World.Destroy(deferred=true) must be rejected today, not silently treated as immediate");
    kb::tests::Require(scene.Entities().IsAlive(liveEntity), "World.Destroy(deferred=true) must not have destroyed the entity when the call itself failed");

    const std::vector<kb::script::ScriptFunctionArgument> explicitImmediateArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ liveEntity.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "deferred", .value = kb::script::ScriptValue{ false } },
    };
    const kb::script::ScriptFunctionCallResult explicitImmediateDestroy = host.Functions().Call("World.Destroy", explicitImmediateArgs, context);
    kb::tests::Require(explicitImmediateDestroy.Succeeded() && explicitImmediateDestroy.Output("destroyed")->AsBool(), "World.Destroy(deferred=false) must behave exactly like the default (immediate) call");
    kb::tests::Require(!scene.Entities().IsAlive(liveEntity), "World.Destroy(deferred=false) must have actually destroyed the entity");
}

// LIB-068: World.IsActive/SetActive — an entity's own fresh Scene/host,
// same reasoning as RunWorldDestroyDeferredFlagTest (keep create/destroy/
// mutate cycles out of the large shared integration test).
void RunWorldActiveStateTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "World active-state test host setup failed");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "ActiveStateEntity" });
    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };
    const std::vector<kb::script::ScriptFunctionArgument> queryArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } },
    };
    const kb::script::ScriptFunctionCallResult initiallyActive = host.Functions().Call("World.IsActive", queryArgs, context);
    kb::tests::Require(initiallyActive.Succeeded() && initiallyActive.Output("active")->AsBool(), "World.IsActive must report true by default for a freshly created entity");

    const std::vector<kb::script::ScriptFunctionArgument> deactivateArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "active", .value = kb::script::ScriptValue{ false } },
    };
    const kb::script::ScriptFunctionCallResult deactivated = host.Functions().Call("World.SetActive", deactivateArgs, context);
    kb::tests::Require(deactivated.Succeeded() && deactivated.Output("set")->AsBool(), "World.SetActive must report set=true for a live entity");
    kb::tests::Require(!scene.Entities().IsActive(entity), "World.SetActive(active=false) must be reflected by kb::scene::SceneEntities::IsActive");
    const kb::script::ScriptFunctionCallResult nowInactive = host.Functions().Call("World.IsActive", queryArgs, context);
    kb::tests::Require(nowInactive.Succeeded() && !nowInactive.Output("active")->AsBool(), "World.IsActive must reflect a prior World.SetActive(active=false)");
    kb::tests::Require(scene.Entities().IsAlive(entity), "World.SetActive(active=false) must not destroy the entity — deactivation is not destruction");

    const std::vector<kb::script::ScriptFunctionArgument> reactivateArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "active", .value = kb::script::ScriptValue{ true } },
    };
    const kb::script::ScriptFunctionCallResult reactivated = host.Functions().Call("World.SetActive", reactivateArgs, context);
    kb::tests::Require(reactivated.Succeeded() && reactivated.Output("set")->AsBool(), "World.SetActive must report set=true when reactivating");
    kb::tests::Require(scene.Entities().IsActive(entity), "World.SetActive(active=true) must reactivate the entity");

    scene.Entities().Destroy(entity);
    const kb::script::ScriptFunctionCallResult deadEntityQuery = host.Functions().Call("World.IsActive", queryArgs, context);
    kb::tests::Require(deadEntityQuery.Succeeded() && !deadEntityQuery.Output("active")->AsBool(), "World.IsActive must report false (not throw) for a destroyed entity");
    const kb::script::ScriptFunctionCallResult deadEntitySet = host.Functions().Call("World.SetActive", deactivateArgs, context);
    kb::tests::Require(deadEntitySet.Succeeded() && !deadEntitySet.Output("set")->AsBool(), "World.SetActive must report set=false (not throw) when targeting a destroyed entity");
}

// LIB-069: World.FindAllByTag — own fresh Scene/host, same reasoning as
// the LIB-067/068 tests above.
void RunWorldFindAllByTagTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "World.FindAllByTag test host setup failed");

    const kb::scene::SceneEntity enemyA = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "EnemyA" });
    const kb::scene::SceneEntity neutral = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Neutral" });
    const kb::scene::SceneEntity enemyB = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "EnemyB" });
    const kb::scene::SceneEntity enemyC = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "EnemyC" });
    kb::scene::TagsComponent enemyTags;
    kb::scene::SetTagsText(enemyTags, "Enemy");
    scene.Components().Tags().Set(enemyA, enemyTags);
    scene.Components().Tags().Set(enemyB, enemyTags);
    scene.Components().Tags().Set(enemyC, enemyTags);

    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };
    std::vector<kb::scene::SceneEntity> foundInOrder;
    int skip = 0;
    // The whole point under test: repeatedly calling with an increasing
    // "skip" must enumerate every tagged entity exactly once and then
    // terminate (an invalid entity), never loop forever and never miss or
    // duplicate a match.
    for (int iteration = 0; iteration < 10; ++iteration) {
        const std::vector<kb::script::ScriptFunctionArgument> args{
            kb::script::ScriptFunctionArgument{ .name = "tag", .value = kb::script::ScriptValue{ std::string{ "Enemy" } } },
            kb::script::ScriptFunctionArgument{ .name = "skip", .value = kb::script::ScriptValue{ skip } },
        };
        const kb::script::ScriptFunctionCallResult result = host.Functions().Call("World.FindAllByTag", args, context);
        kb::tests::Require(result.Succeeded(), "World.FindAllByTag direct call failed");
        const kb::scene::SceneEntity found{ result.Output("entity")->AsUInt64() };
        if (!found.IsValid()) {
            break;
        }
        foundInOrder.push_back(found);
        ++skip;
    }
    kb::tests::Require(foundInOrder.size() == 3U, "World.FindAllByTag must enumerate exactly the three tagged entities, no more, no less");
    kb::tests::Require(
        std::find(foundInOrder.begin(), foundInOrder.end(), enemyA) != foundInOrder.end() && std::find(foundInOrder.begin(), foundInOrder.end(), enemyB) != foundInOrder.end() &&
            std::find(foundInOrder.begin(), foundInOrder.end(), enemyC) != foundInOrder.end(),
        "World.FindAllByTag must find all three differently-tagged entities across repeated calls, not just the first");
    kb::tests::Require(std::find(foundInOrder.begin(), foundInOrder.end(), neutral) == foundInOrder.end(), "World.FindAllByTag must never return an entity that does not have the requested tag");

    const std::vector<kb::script::ScriptFunctionArgument> noMatchArgs{
        kb::script::ScriptFunctionArgument{ .name = "tag", .value = kb::script::ScriptValue{ std::string{ "NoSuchTag" } } },
    };
    const kb::script::ScriptFunctionCallResult noMatchResult = host.Functions().Call("World.FindAllByTag", noMatchArgs, context);
    kb::tests::Require(noMatchResult.Succeeded() && !kb::scene::SceneEntity{ noMatchResult.Output("entity")->AsUInt64() }.IsValid(),
        "World.FindAllByTag must return an invalid entity (not error) when nothing matches, even at skip=0");

    const std::vector<kb::script::ScriptFunctionArgument> pastEndArgs{
        kb::script::ScriptFunctionArgument{ .name = "tag", .value = kb::script::ScriptValue{ std::string{ "Enemy" } } },
        kb::script::ScriptFunctionArgument{ .name = "skip", .value = kb::script::ScriptValue{ 3 } },
    };
    const kb::script::ScriptFunctionCallResult pastEndResult = host.Functions().Call("World.FindAllByTag", pastEndArgs, context);
    kb::tests::Require(pastEndResult.Succeeded() && !kb::scene::SceneEntity{ pastEndResult.Output("entity")->AsUInt64() }.IsValid(),
        "World.FindAllByTag(skip past the last match) must return an invalid entity, not error or wrap around");
}

// LIB-070: World.SetProperty<Type> — data overrides on an ARBITRARY
// entity (not the calling behaviour's own Self, which is all the
// pre-existing Self.SetProperty sugar could target). Own fresh Scene/host,
// same reasoning as the LIB-067/068/069 tests above.
void RunWorldSetPropertyTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "World.SetProperty test host setup failed");

    const kb::scene::SceneObject cameraObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Camera" });
    scene.Components().Cameras().Set(cameraObject.Entity(), kb::scene::CameraComponent{});
    const kb::scene::SceneObject behaviourObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Behaviour" });
    scene.Components().Behaviours().Set(behaviourObject.Entity(), kb::scene::BehaviourComponent{ .enabled = true });

    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };

    // A property setter is only meaningfully proven by first observing
    // the property's default, then observing it changed — not merely
    // that the call reported success.
    const float defaultFov = scene.Components().Cameras().TryGet(cameraObject.Entity())->verticalFovDegrees;
    const std::vector<kb::script::ScriptFunctionArgument> setFovArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ cameraObject.Entity().Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "component", .value = kb::script::ScriptValue{ std::string{ "Camera" } } },
        kb::script::ScriptFunctionArgument{ .name = "property", .value = kb::script::ScriptValue{ std::string{ "verticalFovDegrees" } } },
        kb::script::ScriptFunctionArgument{ .name = "value", .value = kb::script::ScriptValue{ 90.0F } },
    };
    const kb::script::ScriptFunctionCallResult setFovResult = host.Functions().Call("World.SetPropertyFloat", setFovArgs, context);
    kb::tests::Require(setFovResult.Succeeded() && setFovResult.Output("set")->AsBool(), "World.SetPropertyFloat must report set=true for a real, writable property");
    kb::tests::Require(!kb::tests::NearlyEqual(scene.Components().Cameras().TryGet(cameraObject.Entity())->verticalFovDegrees, defaultFov)
            && kb::tests::NearlyEqual(scene.Components().Cameras().TryGet(cameraObject.Entity())->verticalFovDegrees, 90.0F),
        "World.SetPropertyFloat must actually change the target entity's live component data, on an entity that is NOT the caller's own Self");

    const std::vector<kb::script::ScriptFunctionArgument> disableArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ behaviourObject.Entity().Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "component", .value = kb::script::ScriptValue{ std::string{ "Behaviour" } } },
        kb::script::ScriptFunctionArgument{ .name = "property", .value = kb::script::ScriptValue{ std::string{ "enabled" } } },
        kb::script::ScriptFunctionArgument{ .name = "value", .value = kb::script::ScriptValue{ false } },
    };
    const kb::script::ScriptFunctionCallResult disableResult = host.Functions().Call("World.SetPropertyBool", disableArgs, context);
    kb::tests::Require(disableResult.Succeeded() && disableResult.Output("set")->AsBool(), "World.SetPropertyBool must report set=true for a real, writable property");
    kb::tests::Require(!scene.Components().Behaviours().TryGet(behaviourObject.Entity())->enabled, "World.SetPropertyBool must actually change the target entity's live component data");

    const std::vector<kb::script::ScriptFunctionArgument> unknownComponentArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ cameraObject.Entity().Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "component", .value = kb::script::ScriptValue{ std::string{ "NoSuchComponent" } } },
        kb::script::ScriptFunctionArgument{ .name = "property", .value = kb::script::ScriptValue{ std::string{ "value" } } },
        kb::script::ScriptFunctionArgument{ .name = "value", .value = kb::script::ScriptValue{ 1.0F } },
    };
    const kb::script::ScriptFunctionCallResult unknownComponentResult = host.Functions().Call("World.SetPropertyFloat", unknownComponentArgs, context);
    kb::tests::Require(unknownComponentResult.Succeeded() && !unknownComponentResult.Output("set")->AsBool(),
        "World.SetPropertyFloat targeting an unknown component must report set=false, not error or crash");

    const kb::scene::SceneEntity destroyed = scene.Entities().CreateEntity();
    scene.Entities().Destroy(destroyed);
    const std::vector<kb::script::ScriptFunctionArgument> deadEntityArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ destroyed.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "component", .value = kb::script::ScriptValue{ std::string{ "Camera" } } },
        kb::script::ScriptFunctionArgument{ .name = "property", .value = kb::script::ScriptValue{ std::string{ "verticalFovDegrees" } } },
        kb::script::ScriptFunctionArgument{ .name = "value", .value = kb::script::ScriptValue{ 45.0F } },
    };
    const kb::script::ScriptFunctionCallResult deadEntityResult = host.Functions().Call("World.SetPropertyFloat", deadEntityArgs, context);
    kb::tests::Require(deadEntityResult.Succeeded() && !deadEntityResult.Output("set")->AsBool(), "World.SetPropertyFloat targeting a destroyed entity must report set=false, not throw");
}

// LIB-070 ("ownership control"): proves that destroying the entity handle
// World.InstantiatePrefab/World.Spawn(prefab=...) returns really does
// relinquish the WHOLE instantiated hierarchy, not just the root — the
// caller that owns the returned handle owns (and can fully release) the
// entire prefab instance via the ordinary World.Destroy, with no separate
// "destroy the whole instance" API needed. Verified against a REAL
// multi-object prefab (root + child), not the single-object fixture used
// elsewhere in this file.
void RunWorldInstantiatePrefabOwnershipTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "PrefabOwnershipProject";
    const std::filesystem::path prefabPath = projectRoot / "Assets" / "Prefabs" / "ParentChildPrefab.kbprefab";
    {
        kb::scene::Scene prefabSource;
        const kb::scene::SceneObject prefabRoot = prefabSource.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Root" });
        const kb::scene::SceneObject prefabChild = prefabSource.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Child", .parent = prefabRoot });
        static_cast<void>(prefabChild);
        const kb::scene::ScenePrefabHandle prefab = prefabSource.Prefabs().CaptureRegistered(prefabRoot, "ParentChildPrefab");
        kb::tests::Require(prefabSource.Prefabs().Save(prefab, prefabPath), "Prefab ownership test fixture was not saved");
    }

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Prefab ownership test project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Prefab ownership test prefab was not discovered");
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Prefab ownership test host setup failed");

    const std::vector<kb::script::ScriptFunctionArgument> instantiateArgs{
        kb::script::ScriptFunctionArgument{ .name = "prefab", .value = kb::script::ScriptValue{ std::string{ "/Game/Prefabs/ParentChildPrefab.kbprefab" } } },
    };
    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };
    const kb::script::ScriptFunctionCallResult instantiated = host.Functions().Call("World.InstantiatePrefab", instantiateArgs, context);
    kb::tests::Require(instantiated.Succeeded(), "World.InstantiatePrefab direct call failed");
    kb::tests::Require(instantiated.Output("count")->AsInt() == 2, "World.InstantiatePrefab must report both the root and its child in count");
    const kb::scene::SceneEntity root{ instantiated.Output("entity")->AsUInt64() };
    kb::tests::Require(root.IsValid() && scene.Entities().IsAlive(root), "World.InstantiatePrefab must return a live root entity");
    const std::vector<kb::scene::SceneEntity> children = scene.Hierarchy().ChildEntities(root);
    kb::tests::Require(children.size() == 1U && scene.Entities().IsAlive(children.front()), "The instantiated prefab's child must be alive and parented under the returned root");
    const kb::scene::SceneEntity child = children.front();

    const std::vector<kb::script::ScriptFunctionArgument> destroyArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ root.Id(), kb::script::ScriptValueType::Entity } },
    };
    const kb::script::ScriptFunctionCallResult destroyResult = host.Functions().Call("World.Destroy", destroyArgs, context);
    kb::tests::Require(destroyResult.Succeeded() && destroyResult.Output("destroyed")->AsBool(), "World.Destroy on the instantiated prefab's root must succeed");
    kb::tests::Require(!scene.Entities().IsAlive(root), "World.Destroy must have destroyed the prefab instance's root");
    kb::tests::Require(!scene.Entities().IsAlive(child),
        "World.Destroy on the prefab instance's root must cascade to destroy its child too — the caller that owns the returned handle owns the WHOLE instantiated hierarchy, not just the root");
}

// LIB-071: Scene.Load/Unload/SetActive/GetActive/Find/LoadProgress — proves
// additive loading genuinely preserves prior content (not merely
// non-destructive in name), selective Unload only removes its own record's
// entities, non-additive Load still replaces the whole scene (matching
// SceneDocumentService::LoadIntoScene's pre-existing ClearSceneRoots
// behaviour), and Progress honestly reports 1.0/0.0 rather than a
// fabricated in-between value. Own fresh Scene/host — same isolated-scene
// pattern as the LIB-067/068/069/070 tests above.
void RunSceneLoadedContentApiTest() {
    ResetTestRoot();
    const std::filesystem::path sceneAFile = TestRoot() / "SceneLoadedContentProject" / "SceneA.21kbscene";
    const std::filesystem::path sceneBFile = TestRoot() / "SceneLoadedContentProject" / "SceneB.21kbscene";
    {
        kb::scene::Scene sourceA;
        static_cast<void>(sourceA.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "RootA" }));
        kb::tests::Require(kb::scene::SceneDocumentService::Save(sourceA, sceneAFile, "SceneA"), "Scene.Load test fixture SceneA was not saved");
    }
    {
        kb::scene::Scene sourceB;
        static_cast<void>(sourceB.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "RootB" }));
        kb::tests::Require(kb::scene::SceneDocumentService::Save(sourceB, sceneBFile, "SceneB"), "Scene.Load test fixture SceneB was not saved");
    }

    kb::scene::Scene scene;
    static_cast<void>(scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "PreexistingRoot" }));
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Scene.Load test host setup failed");
    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };

    const std::vector<kb::script::ScriptFunctionArgument> loadAArgs{
        kb::script::ScriptFunctionArgument{ .name = "path", .value = kb::script::ScriptValue{ sceneAFile.string() } },
    };
    const kb::script::ScriptFunctionCallResult loadAResult = host.Functions().Call("Scene.Load", loadAArgs, context);
    kb::tests::Require(loadAResult.Succeeded(), "Scene.Load (non-additive) direct call failed");
    const std::uint64_t idA = loadAResult.Output("id")->AsUInt64();
    kb::tests::Require(idA != 0U, "Scene.Load must return a nonzero id for a successful load");
    {
        const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
        kb::tests::Require(roots.size() == 1U && scene.Entities().Name(roots.front()) == "RootA",
            "Scene.Load(additive=false) must replace the scene's entire prior content with the newly loaded document");
    }

    const kb::script::ScriptFunctionCallResult getActiveAfterA = host.Functions().Call("Scene.GetActive", {}, context);
    kb::tests::Require(getActiveAfterA.Succeeded() && getActiveAfterA.Output("id")->AsUInt64() == idA, "Scene.GetActive must report the just-loaded scene as active");

    const std::vector<kb::script::ScriptFunctionArgument> findAArgs{
        kb::script::ScriptFunctionArgument{ .name = "name", .value = kb::script::ScriptValue{ std::string{ "SceneA" } } },
    };
    const kb::script::ScriptFunctionCallResult findAResult = host.Functions().Call("Scene.Find", findAArgs, context);
    kb::tests::Require(findAResult.Succeeded() && findAResult.Output("id")->AsUInt64() == idA, "Scene.Find must resolve the loaded document's own name back to its id");

    const std::vector<kb::script::ScriptFunctionArgument> loadBArgs{
        kb::script::ScriptFunctionArgument{ .name = "path", .value = kb::script::ScriptValue{ sceneBFile.string() } },
        kb::script::ScriptFunctionArgument{ .name = "additive", .value = kb::script::ScriptValue{ true } },
    };
    const kb::script::ScriptFunctionCallResult loadBResult = host.Functions().Call("Scene.Load", loadBArgs, context);
    kb::tests::Require(loadBResult.Succeeded(), "Scene.Load (additive) direct call failed");
    const std::uint64_t idB = loadBResult.Output("id")->AsUInt64();
    kb::tests::Require(idB != 0U && idB != idA, "Scene.Load(additive=true) must return a distinct nonzero id from the earlier load");

    {
        const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
        kb::tests::Require(roots.size() == 2U, "Scene.Load(additive=true) must instantiate alongside SceneA's content, not replace it");
        const bool hasRootA = std::any_of(roots.begin(), roots.end(), [&scene](kb::scene::SceneEntity entity) { return scene.Entities().Name(entity) == "RootA"; });
        const bool hasRootB = std::any_of(roots.begin(), roots.end(), [&scene](kb::scene::SceneEntity entity) { return scene.Entities().Name(entity) == "RootB"; });
        kb::tests::Require(hasRootA && hasRootB, "Scene.Load(additive=true) must leave SceneA's root alive alongside the newly loaded SceneB's root");
    }

    const kb::script::ScriptFunctionCallResult getActiveAfterB = host.Functions().Call("Scene.GetActive", {}, context);
    kb::tests::Require(getActiveAfterB.Succeeded() && getActiveAfterB.Output("id")->AsUInt64() == idA,
        "Scene.GetActive must remain SceneA's id after an additive load onto an already-active scene — additive Load only sets the active id when nothing was active yet");

    const std::vector<kb::script::ScriptFunctionArgument> progressBArgs{
        kb::script::ScriptFunctionArgument{ .name = "id", .value = kb::script::ScriptValue{ idB, kb::script::ScriptValueType::Hash } },
    };
    const kb::script::ScriptFunctionCallResult progressBResult = host.Functions().Call("Scene.LoadProgress", progressBArgs, context);
    kb::tests::Require(progressBResult.Succeeded() && kb::tests::NearlyEqual(progressBResult.Output("progress")->AsFloat(), 1.0F), "Scene.LoadProgress must report 1.0 for a currently-loaded id");

    const std::vector<kb::script::ScriptFunctionArgument> progressUnknownArgs{
        kb::script::ScriptFunctionArgument{ .name = "id", .value = kb::script::ScriptValue{ std::uint64_t{ 999999U }, kb::script::ScriptValueType::Hash } },
    };
    const kb::script::ScriptFunctionCallResult progressUnknownResult = host.Functions().Call("Scene.LoadProgress", progressUnknownArgs, context);
    kb::tests::Require(progressUnknownResult.Succeeded() && kb::tests::NearlyEqual(progressUnknownResult.Output("progress")->AsFloat(), 0.0F), "Scene.LoadProgress must report 0.0 for an unknown id");

    const std::vector<kb::script::ScriptFunctionArgument> setActiveBArgs{
        kb::script::ScriptFunctionArgument{ .name = "id", .value = kb::script::ScriptValue{ idB, kb::script::ScriptValueType::Hash } },
    };
    const kb::script::ScriptFunctionCallResult setActiveBResult = host.Functions().Call("Scene.SetActive", setActiveBArgs, context);
    kb::tests::Require(setActiveBResult.Succeeded() && setActiveBResult.Output("set")->AsBool(), "Scene.SetActive must succeed for a currently-loaded id");
    const kb::script::ScriptFunctionCallResult getActiveAfterSet = host.Functions().Call("Scene.GetActive", {}, context);
    kb::tests::Require(getActiveAfterSet.Succeeded() && getActiveAfterSet.Output("id")->AsUInt64() == idB, "Scene.GetActive must reflect SetActive's new selection");

    const std::vector<kb::script::ScriptFunctionArgument> unloadAArgs{
        kb::script::ScriptFunctionArgument{ .name = "id", .value = kb::script::ScriptValue{ idA, kb::script::ScriptValueType::Hash } },
    };
    const kb::script::ScriptFunctionCallResult unloadAResult = host.Functions().Call("Scene.Unload", unloadAArgs, context);
    kb::tests::Require(unloadAResult.Succeeded() && unloadAResult.Output("unloaded")->AsBool(), "Scene.Unload must succeed for a currently-loaded id");
    {
        const std::vector<kb::scene::SceneEntity> roots = scene.Hierarchy().RootEntities();
        kb::tests::Require(roots.size() == 1U && scene.Entities().Name(roots.front()) == "RootB",
            "Scene.Unload(idA) must destroy only SceneA's root, leaving SceneB's content untouched");
    }

    const kb::script::ScriptFunctionCallResult progressAfterUnloadResult = host.Functions().Call("Scene.LoadProgress", unloadAArgs, context);
    kb::tests::Require(progressAfterUnloadResult.Succeeded() && kb::tests::NearlyEqual(progressAfterUnloadResult.Output("progress")->AsFloat(), 0.0F),
        "Scene.LoadProgress must report 0.0 for an id that was just Unloaded");

    const kb::script::ScriptFunctionCallResult doubleUnloadResult = host.Functions().Call("Scene.Unload", unloadAArgs, context);
    kb::tests::Require(doubleUnloadResult.Succeeded() && !doubleUnloadResult.Output("unloaded")->AsBool(),
        "Scene.Unload on an already-unloaded id must report unloaded=false, not error or crash");

    const std::vector<kb::script::ScriptFunctionArgument> missingPathArgs{
        kb::script::ScriptFunctionArgument{ .name = "path", .value = kb::script::ScriptValue{ (TestRoot() / "SceneLoadedContentProject" / "NoSuchScene.21kbscene").string() } },
    };
    const kb::script::ScriptFunctionCallResult missingPathResult = host.Functions().Call("Scene.Load", missingPathArgs, context);
    kb::tests::Require(!missingPathResult.Succeeded(), "Scene.Load must fail (not fabricate an id) for a nonexistent path");
}

// LIB-072: World.IsPersistent/SetPersistent basic contract — mirrors
// RunWorldActiveStateTest's shape (LIB-068): default false, SetPersistent
// does NOT itself destroy/move the entity, state reflects immediately,
// dead entity reports false/set=false rather than throwing.
void RunWorldPersistentStateTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "World.SetPersistent test host setup failed");

    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Candidate" });
    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };

    const std::vector<kb::script::ScriptFunctionArgument> isPersistentArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } },
    };
    const kb::script::ScriptFunctionCallResult defaultResult = host.Functions().Call("World.IsPersistent", isPersistentArgs, context);
    kb::tests::Require(defaultResult.Succeeded() && !defaultResult.Output("persistent")->AsBool(), "World.IsPersistent must default to false for a freshly created entity");

    const std::vector<kb::script::ScriptFunctionArgument> setTrueArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "persistent", .value = kb::script::ScriptValue{ true } },
    };
    const kb::script::ScriptFunctionCallResult setTrueResult = host.Functions().Call("World.SetPersistent", setTrueArgs, context);
    kb::tests::Require(setTrueResult.Succeeded() && setTrueResult.Output("set")->AsBool(), "World.SetPersistent must report set=true for a live entity");
    kb::tests::Require(scene.Entities().IsAlive(entity), "World.SetPersistent must not itself destroy or otherwise remove the entity");
    const kb::script::ScriptFunctionCallResult afterSetResult = host.Functions().Call("World.IsPersistent", isPersistentArgs, context);
    kb::tests::Require(afterSetResult.Succeeded() && afterSetResult.Output("persistent")->AsBool(), "World.IsPersistent must reflect an immediately preceding SetPersistent(true)");

    const std::vector<kb::script::ScriptFunctionArgument> setFalseArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ entity.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "persistent", .value = kb::script::ScriptValue{ false } },
    };
    const kb::script::ScriptFunctionCallResult setFalseResult = host.Functions().Call("World.SetPersistent", setFalseArgs, context);
    kb::tests::Require(setFalseResult.Succeeded() && setFalseResult.Output("set")->AsBool(), "World.SetPersistent(false) must report set=true for a live entity");
    const kb::script::ScriptFunctionCallResult afterClearResult = host.Functions().Call("World.IsPersistent", isPersistentArgs, context);
    kb::tests::Require(afterClearResult.Succeeded() && !afterClearResult.Output("persistent")->AsBool(), "World.IsPersistent must reflect a following SetPersistent(false)");

    const kb::scene::SceneEntity destroyed = scene.Entities().CreateEntity();
    scene.Entities().Destroy(destroyed);
    const std::vector<kb::script::ScriptFunctionArgument> deadArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ destroyed.Id(), kb::script::ScriptValueType::Entity } },
    };
    const kb::script::ScriptFunctionCallResult deadIsPersistentResult = host.Functions().Call("World.IsPersistent", deadArgs, context);
    kb::tests::Require(deadIsPersistentResult.Succeeded() && !deadIsPersistentResult.Output("persistent")->AsBool(), "World.IsPersistent on a destroyed entity must report false, not throw");
    const std::vector<kb::script::ScriptFunctionArgument> deadSetArgs{
        kb::script::ScriptFunctionArgument{ .name = "entity", .value = kb::script::ScriptValue{ destroyed.Id(), kb::script::ScriptValueType::Entity } },
        kb::script::ScriptFunctionArgument{ .name = "persistent", .value = kb::script::ScriptValue{ true } },
    };
    const kb::script::ScriptFunctionCallResult deadSetResult = host.Functions().Call("World.SetPersistent", deadSetArgs, context);
    kb::tests::Require(deadSetResult.Succeeded() && !deadSetResult.Output("set")->AsBool(), "World.SetPersistent on a destroyed entity must report set=false, not throw");
}

// LIB-072: the real, end-to-end proof — a persistent ROOT entity (with a
// child, proving the WHOLE hierarchy survives via cascade, not just the
// root itself) genuinely survives a non-additive Scene.Load, while a
// non-persistent root in the same scene is destroyed exactly as before
// LIB-072. Also proves the LIB-071/LIB-072 interaction fix: the freshly
// loaded document's own root is still correctly tracked by
// SceneLoadedContentService (Scene.Find resolves to a record whose root is
// the NEW entity, not the surviving persistent one) even though
// RootEntities() now returns both after the load.
void RunScenePersistentEntitySurvivesLoadTest() {
    ResetTestRoot();
    const std::filesystem::path sceneFile = TestRoot() / "ScenePersistenceProject" / "GameplayScene.21kbscene";
    {
        kb::scene::Scene source;
        static_cast<void>(source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "GameplayRoot" }));
        kb::tests::Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "GameplayScene"), "Scene persistence test fixture GameplayScene was not saved");
    }

    kb::scene::Scene scene;
    const kb::scene::SceneObject persistentRoot = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Bootstrap" });
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "BootstrapChild", .parent = persistentRoot }));
    const kb::scene::SceneEntity transientRoot = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "OldLevelContent" });
    scene.Entities().SetPersistent(persistentRoot.Entity(), true);

    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Scene persistence test host setup failed");
    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };

    const std::vector<kb::script::ScriptFunctionArgument> loadArgs{
        kb::script::ScriptFunctionArgument{ .name = "path", .value = kb::script::ScriptValue{ sceneFile.string() } },
    };
    const kb::script::ScriptFunctionCallResult loadResult = host.Functions().Call("Scene.Load", loadArgs, context);
    kb::tests::Require(loadResult.Succeeded(), "Scene.Load (non-additive, over a persistent entity) direct call failed");
    const std::uint64_t loadedId = loadResult.Output("id")->AsUInt64();
    kb::tests::Require(loadedId != 0U, "Scene.Load must still return a nonzero id when a persistent entity is present");

    kb::tests::Require(scene.Entities().IsAlive(persistentRoot.Entity()), "A persistent root entity must survive a non-additive Scene.Load");
    const std::vector<kb::scene::SceneEntity> survivingChildren = scene.Hierarchy().ChildEntities(persistentRoot.Entity());
    kb::tests::Require(survivingChildren.size() == 1U && scene.Entities().IsAlive(survivingChildren.front()) && scene.Entities().Name(survivingChildren.front()) == "BootstrapChild",
        "A persistent root's WHOLE hierarchy must survive (cascade), not just the root entity itself");
    kb::tests::Require(!scene.Entities().IsAlive(transientRoot), "A non-persistent root must still be destroyed by a non-additive Scene.Load, exactly as before LIB-072");

    const std::vector<kb::scene::SceneEntity> rootsAfterLoad = scene.Hierarchy().RootEntities();
    kb::tests::Require(rootsAfterLoad.size() == 2U, "After the load, exactly the persistent survivor and the newly loaded document's root must remain as roots");
    const bool hasPersistentRoot = std::any_of(rootsAfterLoad.begin(), rootsAfterLoad.end(), [&](kb::scene::SceneEntity e) { return e == persistentRoot.Entity(); });
    const bool hasGameplayRoot = std::any_of(rootsAfterLoad.begin(), rootsAfterLoad.end(), [&scene](kb::scene::SceneEntity e) { return scene.Entities().Name(e) == "GameplayRoot"; });
    kb::tests::Require(hasPersistentRoot && hasGameplayRoot, "Roots after load must be exactly the persistent survivor plus the newly loaded document's root");

    const std::vector<kb::script::ScriptFunctionArgument> findArgs{
        kb::script::ScriptFunctionArgument{ .name = "name", .value = kb::script::ScriptValue{ std::string{ "GameplayScene" } } },
    };
    const kb::script::ScriptFunctionCallResult findResult = host.Functions().Call("Scene.Find", findArgs, context);
    kb::tests::Require(findResult.Succeeded() && findResult.Output("id")->AsUInt64() == loadedId, "Scene.Find must resolve the newly loaded document's own name back to its id even with a persistent survivor also present as a root");

    // The stronger proof: SceneLoadedContentService's record must have
    // tracked the CORRECT new root (GameplayRoot), not misidentified the
    // persistent survivor as "the newly loaded content" — Unload(loadedId)
    // must destroy ONLY GameplayRoot, leaving the persistent hierarchy
    // completely untouched.
    const std::vector<kb::script::ScriptFunctionArgument> unloadArgs{
        kb::script::ScriptFunctionArgument{ .name = "id", .value = kb::script::ScriptValue{ loadedId, kb::script::ScriptValueType::Hash } },
    };
    const kb::script::ScriptFunctionCallResult unloadResult = host.Functions().Call("Scene.Unload", unloadArgs, context);
    kb::tests::Require(unloadResult.Succeeded() && unloadResult.Output("unloaded")->AsBool(), "Scene.Unload must succeed for the newly loaded document's id");
    kb::tests::Require(scene.Entities().IsAlive(persistentRoot.Entity()) && scene.Entities().IsAlive(survivingChildren.front()),
        "Scene.Unload(loadedId) must NOT touch the persistent survivor or its child — proof that SceneLoadedContentService correctly identified GameplayRoot, not the persistent entity, as the tracked root");
    const std::vector<kb::scene::SceneEntity> rootsAfterUnload = scene.Hierarchy().RootEntities();
    kb::tests::Require(rootsAfterUnload.size() == 1U && rootsAfterUnload.front() == persistentRoot.Entity(), "After Scene.Unload(loadedId), only the persistent root must remain");
}

// LIB-073: proves SceneLoading/SceneLoaded/SceneActivated/SceneUnloading/
// SceneUnloaded genuinely reach a REAL running behaviour through the full
// production path — Scene.Load/Unload (called through
// ScriptFunctionRegistry, exactly as a script would) queue notifications
// via SceneLoadedContentService, which ScriptRuntimeSceneSystem::
// ExecuteFrame drains and turns into a real ScriptEvent broadcast on the
// NEXT scene.Runtime().Update() — not just "DispatchEvent was called
// directly" in isolation, and not synchronously inside the Scene.Load/
// Unload call itself (proven by asserting the received list is still
// empty immediately after each call, before the next Update()).
void RunSceneLifecycleEventsReachBehaviourTest() {
    ResetTestRoot();
    const std::filesystem::path sceneFile = TestRoot() / "SceneLifecycleEventsProject" / "LifecycleScene.21kbscene";
    {
        kb::scene::Scene source;
        static_cast<void>(source.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "LifecycleRoot" }));
        kb::tests::Require(kb::scene::SceneDocumentService::Save(source, sceneFile, "LifecycleScene"), "Scene lifecycle events test fixture was not saved");
    }

    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Scene lifecycle events test host setup failed");

    struct ReceivedRecord {
        std::string name;
        std::uint64_t sceneId = 0U;
        std::string sceneName;
    };
    std::vector<ReceivedRecord> received;
    constexpr kb::assets::AssetId kListenerAsset{ 9101U };
    kb::script::NativeScriptBackend& nativeBackend = host.NativeBackend();
    for (const char* eventName : { "SceneLoading", "SceneLoaded", "SceneActivated", "SceneUnloading", "SceneUnloaded" }) {
        kb::tests::Require(
            nativeBackend.RegisterEvent(kListenerAsset, eventName, [&received](kb::script::ScriptExecutionContext&, const kb::script::ScriptEvent& event) {
                std::uint64_t sceneId = 0U;
                std::string sceneName;
                for (const kb::script::ScriptEventArgument& argument : event.arguments) {
                    if (argument.name == "sceneId") {
                        sceneId = argument.value.AsUInt64();
                    } else if (argument.name == "sceneName") {
                        sceneName = argument.value.AsString();
                    }
                }
                received.push_back(ReceivedRecord{ .name = event.name, .sceneId = sceneId, .sceneName = sceneName });
            }),
            (std::string{ "Scene lifecycle events test event registration failed for " } + eventName).c_str());
    }
    // A listener that survives Scene.Load's ClearSceneRoots must itself be
    // persistent (LIB-072) — exactly the real-world shape this event exists
    // to serve (a bootstrap/manager entity reacting to gameplay scene
    // changes). A non-persistent listener would be destroyed by the very
    // same non-additive Scene.Load call it's supposed to be notified about.
    const kb::scene::SceneObject listener = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Listener" });
    scene.Components().Behaviours().Set(listener.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kListenerAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });
    scene.Entities().SetPersistent(listener.Entity(), true);

    kb::tests::Require(host.InstallSceneSystem(), "Scene lifecycle events test scene system install failed");
    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(received.empty(), "No scene lifecycle events must fire before any Scene.Load/Unload/SetActive call");

    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };
    const std::vector<kb::script::ScriptFunctionArgument> loadArgs{
        kb::script::ScriptFunctionArgument{ .name = "path", .value = kb::script::ScriptValue{ sceneFile.string() } },
    };
    const kb::script::ScriptFunctionCallResult loadResult = host.Functions().Call("Scene.Load", loadArgs, context);
    kb::tests::Require(loadResult.Succeeded(), "Scene.Load direct call failed in lifecycle events test");
    const std::uint64_t loadedId = loadResult.Output("id")->AsUInt64();
    kb::tests::Require(loadedId != 0U, "Scene.Load must succeed in lifecycle events test");
    kb::tests::Require(received.empty(), "Scene lifecycle events must not fire synchronously inside Scene.Load itself — only once drained on the next frame");

    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(received.size() == 3U, "Scene.Load (non-additive, becomes active) must dispatch exactly SceneLoading, SceneLoaded, SceneActivated");
    kb::tests::Require(received[0].name == "SceneLoading" && received[1].name == "SceneLoaded" && received[2].name == "SceneActivated",
        "Scene.Load's events must dispatch in SceneLoading -> SceneLoaded -> SceneActivated order");
    for (const ReceivedRecord& record : received) {
        kb::tests::Require(record.sceneId == loadedId && record.sceneName == "LifecycleScene", "Every Scene.Load lifecycle event must carry the correct sceneId/sceneName payload");
    }
    received.clear();

    const std::vector<kb::script::ScriptFunctionArgument> unloadArgs{
        kb::script::ScriptFunctionArgument{ .name = "id", .value = kb::script::ScriptValue{ loadedId, kb::script::ScriptValueType::Hash } },
    };
    const kb::script::ScriptFunctionCallResult unloadResult = host.Functions().Call("Scene.Unload", unloadArgs, context);
    kb::tests::Require(unloadResult.Succeeded() && unloadResult.Output("unloaded")->AsBool(), "Scene.Unload direct call failed in lifecycle events test");
    kb::tests::Require(received.empty(), "Scene lifecycle events must not fire synchronously inside Scene.Unload itself");

    static_cast<void>(scene.Runtime().Update(0.016F));
    kb::tests::Require(received.size() == 2U && received[0].name == "SceneUnloading" && received[1].name == "SceneUnloaded",
        "Scene.Unload must dispatch exactly SceneUnloading then SceneUnloaded, in order");
    kb::tests::Require(received[0].sceneId == loadedId && received[1].sceneId == loadedId, "Scene.Unload's events must carry the unloaded scene's id");
}

// LIB-045: Math.Clamp/Lerp/InverseLerp/Remap/SmoothStep/MoveTowards/Damp
// must be real, callable script functions (not just native kb::math
// helpers) — reachable through ScriptFunctionRegistry::Call, the single
// choke point every Native/Lua/Visual Graph caller funnels through, with
// known-value correctness, not just "registered".
void RunScriptMathApiTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script math API host did not initialize");
    for (const char* name : {
             "Math.Clamp", "Math.Lerp", "Math.InverseLerp", "Math.Remap", "Math.SmoothStep", "Math.MoveTowards", "Math.Damp",
             "Math.Min", "Math.Max", "Math.Abs", "Math.Sign", "Math.Floor", "Math.Ceil", "Math.Round", "Math.Frac", "Math.Mod",
             "Math.Sqrt", "Math.Pow", "Math.Exp", "Math.Log",
             "Math.Sin", "Math.Cos", "Math.Tan", "Math.Asin", "Math.Acos", "Math.Atan", "Math.Atan2",
             "Math.Dot", "Math.Cross", "Math.Length", "Math.Normalize", "Math.Distance", "Math.Project", "Math.Reflect", "Math.Refract",
             "Math.Angle", "Math.SignedAngle", "Math.Slerp", "Math.LookRotation", "Math.FromToRotation", "Math.RotateTowards",
             "Math.Random01", "Math.Noise1D", "Math.Noise2D", "Math.Noise3D",
             "Math.RandomSeed", "Math.RandomNextUInt32", "Math.RandomNextFloat01", "Math.RandomRange", "Math.RandomRangeInt",
             "Math.Ease",
         }) {
        const std::string message = std::string{ "Script math API function '" } + name + "' was not registered";
        kb::tests::Require(host.Functions().FindSignature(name) != nullptr, message.c_str());
    }

    const kb::script::ScriptFunctionCallContext context{ .scene = &scene };

    const kb::script::ScriptFunctionCallResult clamped = host.Functions().Call(
        "Math.Clamp",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "value", .value = kb::script::ScriptValue{ 15.0F } },
            { .name = "min", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "max", .value = kb::script::ScriptValue{ 10.0F } },
        },
        context);
    kb::tests::Require(clamped.Succeeded() && clamped.Output("result").has_value() && kb::tests::NearlyEqual(clamped.Output("result")->AsFloat(), 10.0F), "Math.Clamp direct call did not clamp above the max");

    const kb::script::ScriptFunctionCallResult lerped = host.Functions().Call(
        "Math.Lerp",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "a", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "b", .value = kb::script::ScriptValue{ 10.0F } },
            { .name = "t", .value = kb::script::ScriptValue{ 0.25F } },
        },
        context);
    kb::tests::Require(lerped.Succeeded() && lerped.Output("result").has_value() && kb::tests::NearlyEqual(lerped.Output("result")->AsFloat(), 2.5F), "Math.Lerp direct call returned the wrong value");

    const kb::script::ScriptFunctionCallResult inverseLerped = host.Functions().Call(
        "Math.InverseLerp",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "a", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "b", .value = kb::script::ScriptValue{ 10.0F } },
            { .name = "value", .value = kb::script::ScriptValue{ 2.5F } },
        },
        context);
    kb::tests::Require(inverseLerped.Succeeded() && inverseLerped.Output("t").has_value() && kb::tests::NearlyEqual(inverseLerped.Output("t")->AsFloat(), 0.25F), "Math.InverseLerp direct call returned the wrong t");

    const kb::script::ScriptFunctionCallResult remapped = host.Functions().Call(
        "Math.Remap",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "value", .value = kb::script::ScriptValue{ 5.0F } },
            { .name = "inMin", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "inMax", .value = kb::script::ScriptValue{ 10.0F } },
            { .name = "outMin", .value = kb::script::ScriptValue{ 100.0F } },
            { .name = "outMax", .value = kb::script::ScriptValue{ 200.0F } },
        },
        context);
    kb::tests::Require(remapped.Succeeded() && remapped.Output("result").has_value() && kb::tests::NearlyEqual(remapped.Output("result")->AsFloat(), 150.0F), "Math.Remap direct call returned the wrong value");

    const kb::script::ScriptFunctionCallResult movedTowards = host.Functions().Call(
        "Math.MoveTowards",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "current", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "target", .value = kb::script::ScriptValue{ 10.0F } },
            { .name = "maxDelta", .value = kb::script::ScriptValue{ 3.0F } },
        },
        context);
    kb::tests::Require(movedTowards.Succeeded() && movedTowards.Output("result").has_value() && kb::tests::NearlyEqual(movedTowards.Output("result")->AsFloat(), 3.0F), "Math.MoveTowards direct call did not move by maxDelta");

    const kb::script::ScriptFunctionCallResult damped = host.Functions().Call(
        "Math.Damp",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "current", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "target", .value = kb::script::ScriptValue{ 10.0F } },
            { .name = "velocity", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "smoothTime", .value = kb::script::ScriptValue{ 1.0F } },
            { .name = "deltaTime", .value = kb::script::ScriptValue{ 0.1F } },
        },
        context);
    kb::tests::Require(
        damped.Succeeded() && damped.Output("value").has_value() && damped.Output("velocity").has_value() &&
            damped.Output("value")->AsFloat() > 0.0F && damped.Output("value")->AsFloat() < 10.0F,
        "Math.Damp direct call did not move current toward target without overshooting");

    const kb::script::ScriptFunctionCallResult maxed = host.Functions().Call(
        "Math.Max",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "a", .value = kb::script::ScriptValue{ 3.0F } },
            { .name = "b", .value = kb::script::ScriptValue{ 7.0F } },
        },
        context);
    kb::tests::Require(maxed.Succeeded() && maxed.Output("result").has_value() && kb::tests::NearlyEqual(maxed.Output("result")->AsFloat(), 7.0F), "Math.Max direct call returned the wrong value");

    const kb::script::ScriptFunctionCallResult modded = host.Functions().Call(
        "Math.Mod",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "value", .value = kb::script::ScriptValue{ -1.0F } },
            { .name = "divisor", .value = kb::script::ScriptValue{ 4.0F } },
        },
        context);
    kb::tests::Require(modded.Succeeded() && modded.Output("result").has_value() && kb::tests::NearlyEqual(modded.Output("result")->AsFloat(), 3.0F), "Math.Mod direct call must use the floor-based convention (mod(-1,4)=3, not -1)");

    const kb::script::ScriptFunctionCallResult sqrted = host.Functions().Call(
        "Math.Sqrt",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "value", .value = kb::script::ScriptValue{ 16.0F } },
        },
        context);
    kb::tests::Require(sqrted.Succeeded() && sqrted.Output("result").has_value() && kb::tests::NearlyEqual(sqrted.Output("result")->AsFloat(), 4.0F), "Math.Sqrt direct call returned the wrong value");

    const kb::script::ScriptFunctionCallResult sined = host.Functions().Call(
        "Math.Sin",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "angle", .value = kb::script::ScriptValue{ kb::math::kPi / 2.0F } },
        },
        context);
    kb::tests::Require(sined.Succeeded() && sined.Output("result").has_value() && kb::tests::NearlyEqual(sined.Output("result")->AsFloat(), 1.0F), "Math.Sin direct call returned the wrong value");

    const kb::script::ScriptFunctionCallResult atan2ed = host.Functions().Call(
        "Math.Atan2",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "y", .value = kb::script::ScriptValue{ 1.0F } },
            { .name = "x", .value = kb::script::ScriptValue{ 1.0F } },
        },
        context);
    kb::tests::Require(atan2ed.Succeeded() && atan2ed.Output("result").has_value() && kb::tests::NearlyEqual(atan2ed.Output("result")->AsFloat(), kb::math::kPi / 4.0F), "Math.Atan2 direct call returned the wrong value");

    // LIB-047's "zdefiniowana domena błędu": Math.Asin with a value
    // outside [-1,1] must fail with a real error, not silently return NaN
    // into the graph.
    const kb::script::ScriptFunctionCallResult asinOutOfDomain = host.Functions().Call(
        "Math.Asin",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "value", .value = kb::script::ScriptValue{ 2.0F } },
        },
        context);
    kb::tests::Require(!asinOutOfDomain.Succeeded() && !asinOutOfDomain.errors.empty(), "Math.Asin with a value outside [-1,1] must report a real error, not succeed with NaN");

    const kb::script::ScriptFunctionCallResult acosOutOfDomain = host.Functions().Call(
        "Math.Acos",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "value", .value = kb::script::ScriptValue{ -1.5F } },
        },
        context);
    kb::tests::Require(!acosOutOfDomain.Succeeded() && !acosOutOfDomain.errors.empty(), "Math.Acos with a value outside [-1,1] must report a real error, not succeed with NaN");

    const kb::script::ScriptFunctionCallResult asinInDomain = host.Functions().Call(
        "Math.Asin",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "value", .value = kb::script::ScriptValue{ 1.0F } },
        },
        context);
    kb::tests::Require(asinInDomain.Succeeded() && asinInDomain.Output("result").has_value() && kb::tests::NearlyEqual(asinInDomain.Output("result")->AsFloat(), kb::math::kPi / 2.0F), "Math.Asin with a boundary-valid value (1.0) must succeed, not be rejected as out of domain");

    // LIB-048: Vec3 is not a script pin type — every Vec3-shaped Math.*
    // function decomposes into named-prefix Float pins (aX/aY/aZ, ...),
    // the same convention Physics.Raycast already uses.
    const kb::script::ScriptFunctionCallResult crossed = host.Functions().Call(
        "Math.Cross",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "aX", .value = kb::script::ScriptValue{ 1.0F } },
            { .name = "aY", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "aZ", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "bX", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "bY", .value = kb::script::ScriptValue{ 1.0F } },
            { .name = "bZ", .value = kb::script::ScriptValue{ 0.0F } },
        },
        context);
    kb::tests::Require(
        crossed.Succeeded() && crossed.Output("x").has_value() && crossed.Output("y").has_value() && crossed.Output("z").has_value() &&
            kb::tests::NearlyEqual(crossed.Output("x")->AsFloat(), 0.0F) && kb::tests::NearlyEqual(crossed.Output("y")->AsFloat(), 0.0F) && kb::tests::NearlyEqual(crossed.Output("z")->AsFloat(), 1.0F),
        "Math.Cross direct call must decompose Vec3 into aX/aY/aZ/bX/bY/bZ pins and return x/y/z outputs");

    const kb::script::ScriptFunctionCallResult distanced = host.Functions().Call(
        "Math.Distance",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "aX", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "aY", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "aZ", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "bX", .value = kb::script::ScriptValue{ 3.0F } },
            { .name = "bY", .value = kb::script::ScriptValue{ 4.0F } },
            { .name = "bZ", .value = kb::script::ScriptValue{ 0.0F } },
        },
        context);
    kb::tests::Require(distanced.Succeeded() && distanced.Output("result").has_value() && kb::tests::NearlyEqual(distanced.Output("result")->AsFloat(), 5.0F), "Math.Distance direct call returned the wrong value");

    const kb::script::ScriptFunctionCallResult angled = host.Functions().Call(
        "Math.Angle",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "aX", .value = kb::script::ScriptValue{ 1.0F } },
            { .name = "aY", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "aZ", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "bX", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "bY", .value = kb::script::ScriptValue{ 1.0F } },
            { .name = "bZ", .value = kb::script::ScriptValue{ 0.0F } },
        },
        context);
    kb::tests::Require(angled.Succeeded() && angled.Output("result").has_value() && kb::tests::NearlyEqual(angled.Output("result")->AsFloat(), kb::math::kPi / 2.0F), "Math.Angle direct call returned the wrong value");

    // LIB-049: Quat is not a script pin type either — decomposed into
    // <prefix>X/Y/Z/W pins, mirroring Vec3's convention.
    const kb::script::ScriptFunctionCallResult lookRotated = host.Functions().Call(
        "Math.LookRotation",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "forwardX", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "forwardY", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "forwardZ", .value = kb::script::ScriptValue{ 1.0F } },
            { .name = "upX", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "upY", .value = kb::script::ScriptValue{ 1.0F } },
            { .name = "upZ", .value = kb::script::ScriptValue{ 0.0F } },
        },
        context);
    kb::tests::Require(
        lookRotated.Succeeded() && lookRotated.Output("w").has_value() && kb::tests::NearlyEqual(lookRotated.Output("w")->AsFloat(), 1.0F),
        "Math.LookRotation(+Z, +Y) direct call must decompose Vec3/Quat pins and return the identity rotation");

    const kb::script::ScriptFunctionCallResult slerpedAtStart = host.Functions().Call(
        "Math.Slerp",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "aX", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "aY", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "aZ", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "aW", .value = kb::script::ScriptValue{ 1.0F } },
            { .name = "bX", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "bY", .value = kb::script::ScriptValue{ 0.7071F } },
            { .name = "bZ", .value = kb::script::ScriptValue{ 0.0F } },
            { .name = "bW", .value = kb::script::ScriptValue{ 0.7071F } },
            { .name = "t", .value = kb::script::ScriptValue{ 0.0F } },
        },
        context);
    kb::tests::Require(slerpedAtStart.Succeeded() && slerpedAtStart.Output("w").has_value() && kb::tests::NearlyEqual(slerpedAtStart.Output("w")->AsFloat(), 1.0F), "Math.Slerp direct call at t=0 must return the start rotation");

    // LIB-050: seed/index are UInt32 (LIB-041), not Int — a ScriptValue
    // constructed from a bare int literal would be rejected by pin
    // validation as a type mismatch, proving the pin type is actually
    // enforced, not just documented.
    const kb::script::ScriptFunctionCallResult randomed = host.Functions().Call(
        "Math.Random01",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "seed", .value = kb::script::ScriptValue{ std::uint32_t{ 42U } } },
            { .name = "index", .value = kb::script::ScriptValue{ std::uint32_t{ 7U } } },
        },
        context);
    kb::tests::Require(randomed.Succeeded() && randomed.Output("result").has_value() && randomed.Output("result")->AsFloat() >= 0.0F && randomed.Output("result")->AsFloat() <= 1.0F, "Math.Random01 direct call must return a value in [0,1]");

    const kb::script::ScriptFunctionCallResult noised = host.Functions().Call(
        "Math.Noise3D",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "x", .value = kb::script::ScriptValue{ 4.0F } },
            { .name = "y", .value = kb::script::ScriptValue{ -2.0F } },
            { .name = "z", .value = kb::script::ScriptValue{ 9.0F } },
            { .name = "seed", .value = kb::script::ScriptValue{ std::uint32_t{ 42U } } },
        },
        context);
    kb::tests::Require(noised.Succeeded() && noised.Output("result").has_value() && noised.Output("result")->AsFloat() == 0.0F, "Math.Noise3D direct call at an integer lattice point must be exactly zero");

    // LIB-051: RandomStream's state (streamSeed/streamCounter, both
    // UInt32) must round-trip through Math.RandomSeed and then thread
    // correctly through Math.RandomRangeInt — proving the {value,
    // advancedStream} pattern actually works end to end through the
    // script boundary, not just natively.
    const kb::script::ScriptFunctionCallResult seeded = host.Functions().Call(
        "Math.RandomSeed",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "seed", .value = kb::script::ScriptValue{ std::uint32_t{ 123U } } },
        },
        context);
    kb::tests::Require(
        seeded.Succeeded() && seeded.Output("streamSeed").has_value() && seeded.Output("streamCounter").has_value() &&
            seeded.Output("streamSeed")->AsUInt32() == 123U && seeded.Output("streamCounter")->AsUInt32() == 0U,
        "Math.RandomSeed must return a stream with the given seed and counter 0");

    const kb::script::ScriptFunctionCallResult rangedInt = host.Functions().Call(
        "Math.RandomRangeInt",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "streamSeed", .value = *seeded.Output("streamSeed") },
            { .name = "streamCounter", .value = *seeded.Output("streamCounter") },
            { .name = "min", .value = kb::script::ScriptValue{ 0 } },
            { .name = "max", .value = kb::script::ScriptValue{ 10 } },
        },
        context);
    kb::tests::Require(
        rangedInt.Succeeded() && rangedInt.Output("value").has_value() && rangedInt.Output("value")->AsInt() >= 0 && rangedInt.Output("value")->AsInt() < 10 &&
            rangedInt.Output("streamCounter").has_value() && rangedInt.Output("streamCounter")->AsUInt32() == 1U,
        "Math.RandomRangeInt direct call must return a value in [0,10) and advance streamCounter by exactly one");

    // LIB-052: Easing is exposed as an Int ordinal (no dedicated enum
    // ScriptValueType), with an out-of-range ordinal rejected as a real
    // domain error (same pattern as LIB-047's Asin/Acos) rather than an
    // unchecked cast into undefined enum territory.
    const kb::script::ScriptFunctionCallResult eased = host.Functions().Call(
        "Math.Ease",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "easing", .value = kb::script::ScriptValue{ static_cast<int>(kb::math::Easing::InQuad) } },
            { .name = "t", .value = kb::script::ScriptValue{ 0.5F } },
        },
        context);
    kb::tests::Require(eased.Succeeded() && eased.Output("result").has_value() && kb::tests::NearlyEqual(eased.Output("result")->AsFloat(), 0.25F), "Math.Ease direct call with InQuad at t=0.5 must return 0.25");

    const kb::script::ScriptFunctionCallResult easedOutOfRange = host.Functions().Call(
        "Math.Ease",
        std::vector<kb::script::ScriptFunctionArgument>{
            { .name = "easing", .value = kb::script::ScriptValue{ 9999 } },
            { .name = "t", .value = kb::script::ScriptValue{ 0.5F } },
        },
        context);
    kb::tests::Require(!easedOutOfRange.Succeeded() && !easedOutOfRange.errors.empty(), "Math.Ease with an out-of-range easing ordinal must report a real error, not cast into undefined enum territory");
}

// LIB-056: Math.Clamp is a single ScriptFunctionRegistry-registered
// function — calling it through Native/Lua/Visual Graph is calling the
// SAME kb::math::Clamp underneath every time (ScriptMathApi.cpp's
// callback), so this is a parity check on the marshalling paths
// (ScriptExecutionContext::CallFunction, Lua's CallFunction global,
// VisualGraph's CallNative binding), not on Clamp's own math — that's
// already covered natively by EngineMathTests.cpp. Reuses exactly the
// three-backend harness RunScriptFunctionRegistryCrossBackendTest (LIB-011)
// already established (Native/Lua/VisualGraph BehaviourComponents driving
// one ScriptRuntime::ExecuteLifecycle Tick), just with Math.Clamp's three
// Float inputs (value/min/max) instead of Inventory.AddItem's single Int.
void RunMathFunctionCrossBackendParityTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Math cross-backend parity host setup failed");
    kb::tests::Require(host.Functions().FindSignature("Math.Clamp") != nullptr, "Math.Clamp must already be registered (LIB-045) before this parity test runs");

    constexpr kb::assets::AssetId kNativeAsset{ 5020U };
    constexpr kb::assets::AssetId kLuaAsset{ 5021U };
    constexpr kb::assets::AssetId kVisualAsset{ 5022U };
    const kb::scene::SceneObject nativeObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Native Math Caller" });
    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lua Math Caller" });
    const kb::scene::SceneObject graphObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Visual Math Caller" });
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
                               kb::script::ScriptFunctionArgument{ .name = "value", .value = kb::script::ScriptValue{ 15.0F } },
                               kb::script::ScriptFunctionArgument{ .name = "min", .value = kb::script::ScriptValue{ 0.0F } },
                               kb::script::ScriptFunctionArgument{ .name = "max", .value = kb::script::ScriptValue{ 10.0F } },
                           };
                           const kb::script::ScriptFunctionCallResult result = context.CallFunction("Math.Clamp", arguments);
                           kb::tests::Require(result.Succeeded(), "Native Math.Clamp call failed");
                           const std::optional<kb::script::ScriptValue> value = result.Output("result");
                           kb::tests::Require(value.has_value(), "Native Math.Clamp call did not return a result");
                           kb::tests::Require(context.SetSharedValue("nativeClampResult", *value), "Native Math.Clamp call could not store shared result");
                       }),
        "Native Math caller did not register");

    const kb::script::PucLuaLoadResult loadedLua = host.LuaRuntime().LoadScript(kLuaAsset, R"(
function Tick(self, dt)
    local result = CallFunction("Math.Clamp", { value = 15.0, min = 0.0, max = 10.0 })
    SetShared("luaClampResult", result)
end
)");
    kb::tests::Require(loadedLua.succeeded, "Lua Math caller did not load");

    kb::visual::VisualGraphAsset graph{};
    graph.name = "VisualMathCaller";
    graph.nodes = {
        kb::visual::VisualGraphNode{ .id = 1U, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick },
        kb::visual::VisualGraphNode{ .id = 2U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "MathClampInputs" },
        kb::visual::VisualGraphNode{ .id = 3U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "GraphClampResultKey" },
        kb::visual::VisualGraphNode{ .id = 4U, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "Function.Math.Clamp" },
        kb::visual::VisualGraphNode{ .id = 5U, .kind = kb::visual::VisualGraphNodeKind::SetProperty, .symbol = "Shared.Set.Float" },
    };
    graph.pins = {
        kb::visual::VisualGraphPin{ .nodeId = 1U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "min", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "max", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "value", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "min", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "max", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 4U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "result", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "key", .type = kb::visual::VisualGraphValueType::String },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "value", .type = kb::visual::VisualGraphValueType::Float },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void },
        kb::visual::VisualGraphPin{ .nodeId = 5U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool },
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{ .fromNode = 1U, .fromPin = "then", .toNode = 4U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
        kb::visual::VisualGraphEdge{ .fromNode = 4U, .fromPin = "then", .toNode = 5U, .toPin = "exec", .kind = kb::visual::VisualGraphEdgeKind::Execution },
        kb::visual::VisualGraphEdge{ .fromNode = 2U, .fromPin = "value", .toNode = 4U, .toPin = "value", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 2U, .fromPin = "min", .toNode = 4U, .toPin = "min", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 2U, .fromPin = "max", .toNode = 4U, .toPin = "max", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 3U, .fromPin = "value", .toNode = 5U, .toPin = "key", .kind = kb::visual::VisualGraphEdgeKind::Data },
        kb::visual::VisualGraphEdge{ .fromNode = 4U, .fromPin = "result", .toNode = 5U, .toPin = "value", .kind = kb::visual::VisualGraphEdgeKind::Data },
    };
    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Visual Math caller graph did not compile");
    host.VisualGraphs().Store(kb::visual::VisualGraphRuntimeArtifact{
        .assetId = kVisualAsset,
        .graphName = graph.name,
        .module = compiled.module,
    });
    kb::tests::Require(host.VisualGraphRuntimeBindings().Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "MathClampInputs",
                           .outputs = {
                               kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Float },
                               kb::visual::VisualGraphPinSignature{ .name = "min", .type = kb::visual::VisualGraphValueType::Float },
                               kb::visual::VisualGraphPinSignature{ .name = "max", .type = kb::visual::VisualGraphValueType::Float },
                           },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ 15.0F });
                               context.Store(instruction.sourceNodeId, "min", kb::visual::VisualGraphRuntimeValue{ 0.0F });
                               context.Store(instruction.sourceNodeId, "max", kb::visual::VisualGraphRuntimeValue{ 10.0F });
                           },
                       }),
        "Visual Math clamp-inputs binding did not register");
    kb::tests::Require(host.VisualGraphRuntimeBindings().Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "GraphClampResultKey",
                           .outputs = { kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::String } },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ std::string{ "graphClampResult" } });
                           },
                       }),
        "Visual Math clamp-result-key binding did not register");

    const kb::script::ScriptRuntimeExecutionResult result = host.Runtime().ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.0F);
    kb::tests::Require(result.Succeeded(), "Math cross-backend parity runtime produced diagnostics");

    const std::optional<kb::script::ScriptValue> nativeResult = host.SharedState().Get("nativeClampResult");
    const std::optional<kb::script::ScriptValue> luaResult = host.SharedState().Get("luaClampResult");
    const std::optional<kb::script::ScriptValue> graphResult = host.SharedState().Get("graphClampResult");
    kb::tests::Require(nativeResult.has_value(), "Native backend did not store a Math.Clamp result");
    kb::tests::Require(luaResult.has_value(), "Lua backend did not store a Math.Clamp result");
    kb::tests::Require(graphResult.has_value(), "Visual Graph backend did not store a Math.Clamp result");

    // The actual parity check: kb::math::Clamp(15, 0, 10) == 10 exactly
    // (no floating-point accumulation in a single Clamp call), and all
    // three backends must agree with each other AND with the expected
    // value, within float tolerance.
    constexpr float kExpected = 10.0F;
    kb::tests::Require(kb::tests::NearlyEqual(nativeResult->AsFloat(), kExpected), "Native Math.Clamp result does not match the expected value");
    kb::tests::Require(kb::tests::NearlyEqual(luaResult->AsFloat(), kExpected), "Lua Math.Clamp result does not match the expected value");
    kb::tests::Require(kb::tests::NearlyEqual(graphResult->AsFloat(), kExpected), "Visual Graph Math.Clamp result does not match the expected value");
    kb::tests::Require(kb::tests::NearlyEqual(nativeResult->AsFloat(), luaResult->AsFloat()), "Native and Lua Math.Clamp results must match within float tolerance");
    kb::tests::Require(kb::tests::NearlyEqual(luaResult->AsFloat(), graphResult->AsFloat()), "Lua and Visual Graph Math.Clamp results must match within float tolerance");
}

void RunScriptInputApiTest() {
    using namespace kb::input;

    auto jump = std::make_shared<InputActionAsset>();
    jump->name = "Jump";
    jump->valueType = InputActionValueType::Bool;

    auto move = std::make_shared<InputActionAsset>();
    move->name = "Move";
    move->valueType = InputActionValueType::Axis1D;

    auto look = std::make_shared<InputActionAsset>();
    look->name = "Look";
    look->valueType = InputActionValueType::Axis2D;

    auto thrust = std::make_shared<InputActionAsset>();
    thrust->name = "Thrust";
    thrust->valueType = InputActionValueType::Axis3D;

    auto context = std::make_shared<InputMappingContextAsset>();
    context->mappings.push_back(InputKeyMapping{ .actionId = 1U, .key = InputKey::Space });
    context->mappings.push_back(InputKeyMapping{ .actionId = 2U, .key = InputKey::W });
    context->mappings.push_back(InputKeyMapping{ .actionId = 3U, .key = InputKey::MouseX });
    context->mappings.push_back(InputKeyMapping{ .actionId = 4U, .key = InputKey::GamepadLeftTrigger });

    std::unordered_map<std::uint64_t, std::shared_ptr<InputActionAsset>> actions{
        { 1U, jump },
        { 2U, move },
        { 3U, look },
        { 4U, thrust },
    };
    std::unordered_map<std::uint64_t, std::shared_ptr<InputMappingContextAsset>> contexts{ { 50U, context } };

    kb::scene::Scene scene;
    scene.Input().SetResolvers(
        [&actions](std::uint64_t id) -> std::shared_ptr<const InputActionAsset> {
            const auto found = actions.find(id);
            return found != actions.end() ? found->second : nullptr;
        },
        [&contexts](std::uint64_t id) -> std::shared_ptr<const InputMappingContextAsset> {
            const auto found = contexts.find(id);
            return found != contexts.end() ? found->second : nullptr;
        });

    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script input API host did not initialize");
    kb::tests::Require(host.Functions().FindSignature("Input.IsPressed") != nullptr, "Input.IsPressed was not registered");
    kb::tests::Require(host.Functions().FindSignature("Input.WasReleased") != nullptr, "Input.WasReleased was not registered");
    kb::tests::Require(host.Functions().FindSignature("Input.Vector3") != nullptr, "Input.Vector3 was not registered");

    const kb::script::ScriptFunctionCallContext callContext{ .scene = &scene, .deltaSeconds = 0.016F };
    const std::vector<kb::script::ScriptFunctionArgument> addContextArgs{
        kb::script::ScriptFunctionArgument{ .name = "context", .value = kb::script::ScriptValue{ std::string{ "50" } } },
        kb::script::ScriptFunctionArgument{ .name = "priority", .value = kb::script::ScriptValue{ 0 } },
    };
    const kb::script::ScriptFunctionCallResult added = host.Functions().Call("Input.AddMappingContext", addContextArgs, callContext);
    kb::tests::Require(added.Succeeded() && added.Output("added").has_value() && added.Output("added")->AsBool(),
        "Input.AddMappingContext direct call failed");

    scene.Input().MutableDeviceState().SetKeyDown(InputKey::Space, true);
    scene.Input().MutableDeviceState().SetKeyDown(InputKey::W, true);
    scene.Input().MutableDeviceState().SetAnalog(InputKey::MouseX, 0.25F);
    scene.Input().MutableDeviceState().SetAnalog(InputKey::GamepadLeftTrigger, 0.75F);
    scene.Input().Evaluate(0.016F);

    const std::vector<kb::script::ScriptFunctionArgument> jumpArgs{
        kb::script::ScriptFunctionArgument{ .name = "action", .value = kb::script::ScriptValue{ std::string{ "Jump" } } },
    };
    const kb::script::ScriptFunctionCallResult isPressed = host.Functions().Call("Input.IsPressed", jumpArgs, callContext);
    const kb::script::ScriptFunctionCallResult wasPressed = host.Functions().Call("Input.WasPressed", jumpArgs, callContext);
    kb::tests::Require(isPressed.Succeeded() && isPressed.Output("pressed")->AsBool(), "Input.IsPressed direct call did not see Jump");
    kb::tests::Require(wasPressed.Succeeded() && wasPressed.Output("pressed")->AsBool(), "Input.WasPressed direct call did not see Jump edge");

    const std::vector<kb::script::ScriptFunctionArgument> moveArgs{
        kb::script::ScriptFunctionArgument{ .name = "action", .value = kb::script::ScriptValue{ std::string{ "Move" } } },
    };
    const kb::script::ScriptFunctionCallResult moveValue = host.Functions().Call("Input.Value", moveArgs, callContext);
    kb::tests::Require(moveValue.Succeeded() && kb::tests::NearlyEqual(moveValue.Output("value")->AsFloat(), 1.0F),
        "Input.Value direct call returned wrong Move value");

    const std::vector<kb::script::ScriptFunctionArgument> lookArgs{
        kb::script::ScriptFunctionArgument{ .name = "action", .value = kb::script::ScriptValue{ std::string{ "Look" } } },
    };
    const kb::script::ScriptFunctionCallResult lookVector = host.Functions().Call("Input.Vector2", lookArgs, callContext);
    kb::tests::Require(lookVector.Succeeded() && kb::tests::NearlyEqual(lookVector.Output("x")->AsFloat(), 0.25F),
        "Input.Vector2 direct call returned wrong Look value");

    const std::vector<kb::script::ScriptFunctionArgument> thrustArgs{
        kb::script::ScriptFunctionArgument{ .name = "action", .value = kb::script::ScriptValue{ std::string{ "Thrust" } } },
    };
    const kb::script::ScriptFunctionCallResult thrustVector = host.Functions().Call("Input.Vector3", thrustArgs, callContext);
    kb::tests::Require(thrustVector.Succeeded() && kb::tests::NearlyEqual(thrustVector.Output("x")->AsFloat(), 0.75F),
        "Input.Vector3 direct call returned wrong Thrust value");

    scene.Input().MutableDeviceState().SetKeyDown(InputKey::Space, false);
    scene.Input().Evaluate(0.016F);

    const kb::assets::AssetId luaAsset{ 8820U };
    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lua Input Caller" });
    scene.Components().Behaviours().Set(luaObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = luaAsset.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });
    const kb::script::PucLuaLoadResult loadedLua = host.LuaRuntime().LoadScript(luaAsset, R"(
function Tick(self, dt)
    local look = Input.Vector2("Look")
    local thrust = Input.Vector3("Thrust")
    SetShared("input.jumpPressed", Input.IsPressed("Jump"))
    SetShared("input.jumpReleased", Input.WasReleased("Jump"))
    SetShared("input.move", Input.Value("Move"))
    SetShared("input.lookX", look.x)
    SetShared("input.thrustX", thrust.x)
    SetShared("input.removed", Input.RemoveMappingContext(50))
end
)");
    kb::tests::Require(loadedLua.succeeded, "Script input API Lua wrapper script did not load");
    const kb::script::ScriptRuntimeExecutionResult tick = host.Runtime().ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.016F);
    kb::tests::Require(tick.Succeeded(), "Script input API Lua wrapper execution failed");
    kb::tests::Require(!host.SharedState().Get("input.jumpPressed")->AsBool(), "Lua Input.IsPressed should be false after Jump release");
    kb::tests::Require(host.SharedState().Get("input.jumpReleased")->AsBool(), "Lua Input.WasReleased did not see Jump release");
    kb::tests::Require(kb::tests::NearlyEqual(host.SharedState().Get("input.move")->AsFloat(), 1.0F), "Lua Input.Value returned wrong Move value");
    kb::tests::Require(kb::tests::NearlyEqual(host.SharedState().Get("input.lookX")->AsFloat(), 0.25F), "Lua Input.Vector2 returned wrong Look value");
    kb::tests::Require(kb::tests::NearlyEqual(host.SharedState().Get("input.thrustX")->AsFloat(), 0.75F), "Lua Input.Vector3 returned wrong Thrust value");
    kb::tests::Require(host.SharedState().Get("input.removed")->AsBool() && !scene.Input().HasMappingContext(50U),
        "Lua Input.RemoveMappingContext did not remove the active context");
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

// LIB-077: exhaustive, name-driven coverage that the generated-accessor
// FieldBinding mechanism (ScriptSceneComponentApi.cpp's KB_BOOL/KB_INT/
// KB_UINT32/KB_FLOAT/KB_NESTED_FLOAT/KB_TICKGROUP/KB_CAMERA_PROJECTION/
// KB_LIGHT_KIND-generated read/write function pairs, replacing the old
// offsetof+reinterpret_cast path) round-trips every field correctly and
// rejects a mismatched ScriptValueType — not just the handful of fields
// RunScriptSceneComponentApiTest already covered. Walks
// ComponentProperties() for all 6 registered components (37 fields total)
// rather than hand-picking a few, so a future field added to any
// component's property-desc table is automatically covered here too.
void RunScriptSceneComponentGeneratedAccessorCoverageTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Generated Accessor Coverage" });
    scene.Components().Cameras().Set(object.Entity(), kb::scene::CameraComponent{});
    scene.Components().Lights().Set(object.Entity(), kb::scene::LightComponent{});
    scene.Components().MeshRenderers().Set(object.Entity(), kb::scene::MeshRendererComponent{});
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{ .behaviourAssetId = 1U, .backend = kb::scene::BehaviourBackend::Native, .enabled = true });

    std::size_t fieldsChecked = 0U;
    for (const std::string_view componentName : kb::script::ScriptSceneComponentApi::ComponentNames()) {
        kb::tests::Require(kb::script::ScriptSceneComponentApi::HasComponent(scene, object.Entity(), componentName),
            "Script component API generated accessor coverage test fixture is missing a component");
        for (const kb::script::ScriptSceneComponentPropertyDesc& property : kb::script::ScriptSceneComponentApi::ComponentProperties(componentName)) {
            ++fieldsChecked;
            const std::string fieldLabel = std::string{ componentName } + "." + std::string{ property.name };

            // A String value never matches Bool/Int/Float — must be
            // rejected for every field, regardless of the field's own
            // type, proving the generated write accessor actually checks
            // ScriptValue::Type() before touching the real field.
            const kb::script::ScriptSceneComponentMutationResult mismatched = kb::script::ScriptSceneComponentApi::SetProperty(
                scene, object.Entity(), componentName, property.name, kb::script::ScriptValue{ std::string{ "wrong type" } });
            kb::tests::Require(!mismatched.succeeded, ("Script component API accepted a mismatched value type for " + fieldLabel).c_str());

            if (!property.writable) {
                continue;
            }

            kb::script::ScriptValue validValue;
            switch (property.type) {
            case kb::script::ScriptValueType::Bool:
                validValue = kb::script::ScriptValue{ true };
                break;
            case kb::script::ScriptValueType::Int:
                // 2 is a safe value for every Int-typed field here,
                // including enum-backed ones with a range check
                // (BehaviourTickGroup::Physics == 2, well within
                // [Input=0, Presentation=5]).
                validValue = kb::script::ScriptValue{ 2 };
                break;
            case kb::script::ScriptValueType::Float:
                validValue = kb::script::ScriptValue{ 2.5F };
                break;
            default:
                kb::tests::Require(false, "Script component API generated accessor coverage test found a property type it does not know how to exercise");
                break;
            }

            const kb::script::ScriptSceneComponentMutationResult set = kb::script::ScriptSceneComponentApi::SetProperty(scene, object.Entity(), componentName, property.name, validValue);
            kb::tests::Require(set.succeeded, ("Script component API rejected a correctly-typed value for " + fieldLabel).c_str());

            const kb::script::ScriptSceneComponentPropertyResult get = kb::script::ScriptSceneComponentApi::GetProperty(scene, object.Entity(), componentName, property.name);
            kb::tests::Require(get.succeeded, ("Script component API could not read back " + fieldLabel).c_str());
            if (property.type == kb::script::ScriptValueType::Float) {
                kb::tests::Require(kb::tests::NearlyEqual(get.value.AsFloat(), 2.5F), ("Script component API did not round-trip " + fieldLabel).c_str());
            } else if (property.type == kb::script::ScriptValueType::Int) {
                kb::tests::Require(get.value.AsInt() == 2, ("Script component API did not round-trip " + fieldLabel).c_str());
            } else if (property.type == kb::script::ScriptValueType::Bool) {
                kb::tests::Require(get.value.AsBool(), ("Script component API did not round-trip " + fieldLabel).c_str());
            }
        }
    }
    kb::tests::Require(fieldsChecked == 37U, "Script component API generated accessor coverage test did not exercise the expected total field count (37) across all 6 components");
}

// LIB-082: defensive regression guard — the KB_ASSERT_NOT_POINTER
// compile-time check (ScriptSceneComponentApi.cpp) already forecloses any
// individual field's OWN declared type being a pointer at the point it is
// wired up, and ScriptValue::Storage (LIB-032, ScriptValue.hpp) already
// forecloses storing one directly. Neither catches a field being wired up
// through a WIDENING ScriptValueType (Int64/UInt32/Double/Entity/Component/
// Hash) capable of carrying a full 64-bit value that COULD, in principle,
// encode a raw pointer's bit pattern — unlike Bool/Int/Float, which are all
// narrower than a pointer on every platform this engine targets. This test
// walks every registered component property and asserts its declared
// ScriptValueType stays within the closed set {Bool, Int, Float} this
// system is actually allowed to expose to Lua/VisualGraph today, so adding
// a wider type to any component's property table requires a conscious,
// reviewed change to this allowlist rather than a silent widening of the
// attack surface.
void RunScriptSceneComponentPropertiesNeverExposeRawPointerTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "No Raw Pointer" });
    scene.Components().Cameras().Set(object.Entity(), kb::scene::CameraComponent{});
    scene.Components().Lights().Set(object.Entity(), kb::scene::LightComponent{});
    scene.Components().MeshRenderers().Set(object.Entity(), kb::scene::MeshRendererComponent{});
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{ .behaviourAssetId = 1U, .backend = kb::scene::BehaviourBackend::Native, .enabled = true });

    std::size_t propertiesChecked = 0U;
    for (const std::string_view componentName : kb::script::ScriptSceneComponentApi::ComponentNames()) {
        for (const kb::script::ScriptSceneComponentPropertyDesc& property : kb::script::ScriptSceneComponentApi::ComponentProperties(componentName)) {
            ++propertiesChecked;
            const std::string fieldLabel = std::string{ componentName } + "." + std::string{ property.name };
            const bool isNarrowValueType = property.type == kb::script::ScriptValueType::Bool ||
                property.type == kb::script::ScriptValueType::Int ||
                property.type == kb::script::ScriptValueType::Float;
            kb::tests::Require(isNarrowValueType, ("Script component property " + fieldLabel + " uses a ScriptValueType wide enough to carry a raw pointer's bit pattern — LIB-082 requires component fields to stay within Bool/Int/Float").c_str());

            const kb::script::ScriptSceneComponentPropertyResult get = kb::script::ScriptSceneComponentApi::GetProperty(scene, object.Entity(), componentName, property.name);
            kb::tests::Require(get.succeeded, ("Script component property " + fieldLabel + " failed to read for LIB-082's raw-pointer audit").c_str());
            kb::tests::Require(get.value.Type() == property.type, ("Script component property " + fieldLabel + "'s returned ScriptValue::Type() must match its declared property type").c_str());
        }
    }
    kb::tests::Require(propertiesChecked == 37U, "LIB-082 raw-pointer audit did not exercise the expected total field count (37) across all 6 components");
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

// LIB-093: Time.Delta/UnscaledDelta/FixedDelta/Elapsed/FrameIndex/
// FixedStepIndex, called through the registry the same way
// RunScriptWorldTimePhysicsApiTest above already exercises World.*. The
// key assertion this test adds beyond that one is the LIB-065 POWRÓT
// resolution: Time.FrameIndex and World.FrameIndex must return the exact
// same value for the same scene, proving they alias the same underlying
// SceneRuntime counter rather than two independent ones.
void RunScriptTimeApiElapsedAndAliasingTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script time API host did not initialize");
    kb::tests::Require(host.Functions().FindSignature("Time.Delta") != nullptr, "Time.Delta was not registered");
    kb::tests::Require(host.Functions().FindSignature("Time.UnscaledDelta") != nullptr, "Time.UnscaledDelta was not registered");
    kb::tests::Require(host.Functions().FindSignature("Time.FixedDelta") != nullptr, "Time.FixedDelta was not registered");
    kb::tests::Require(host.Functions().FindSignature("Time.Elapsed") != nullptr, "Time.Elapsed was not registered");
    kb::tests::Require(host.Functions().FindSignature("Time.FrameIndex") != nullptr, "Time.FrameIndex was not registered");
    kb::tests::Require(host.Functions().FindSignature("Time.FixedStepIndex") != nullptr, "Time.FixedStepIndex was not registered");

    const kb::script::ScriptFunctionCallContext context{
        .scene = &scene,
        .deltaSeconds = 0.25F,
    };

    const kb::script::ScriptFunctionCallResult deltaResult = host.Functions().Call("Time.Delta", {}, context);
    kb::tests::Require(deltaResult.Succeeded() && kb::tests::NearlyEqual(deltaResult.Output("delta")->AsFloat(), 0.25F), "Time.Delta must echo ScriptFunctionCallContext::deltaSeconds");

    const kb::script::ScriptFunctionCallResult unscaledResult = host.Functions().Call("Time.UnscaledDelta", {}, context);
    kb::tests::Require(unscaledResult.Succeeded() && kb::tests::NearlyEqual(unscaledResult.Output("delta")->AsFloat(), 0.25F), "Time.UnscaledDelta must equal Time.Delta today (no time-scale mechanism exists yet)");

    const kb::script::ScriptFunctionCallResult fixedDeltaResult = host.Functions().Call("Time.FixedDelta", {}, context);
    kb::tests::Require(fixedDeltaResult.Succeeded() && kb::tests::NearlyEqual(fixedDeltaResult.Output("delta")->AsFloat(), scene.Runtime().FixedStepSettings().fixedDeltaSeconds), "Time.FixedDelta must reflect SceneRuntime::FixedStepSettings().fixedDeltaSeconds");

    const kb::script::ScriptFunctionCallResult elapsedBeforeUpdate = host.Functions().Call("Time.Elapsed", {}, context);
    kb::tests::Require(elapsedBeforeUpdate.Succeeded() && kb::tests::NearlyEqual(elapsedBeforeUpdate.Output("elapsed")->AsFloat(), 0.0F), "Time.Elapsed must start at 0 before any Update()");

    static_cast<void>(scene.Runtime().Update(0.5F));
    const kb::script::ScriptFunctionCallResult elapsedAfterOneUpdate = host.Functions().Call("Time.Elapsed", {}, context);
    kb::tests::Require(elapsedAfterOneUpdate.Succeeded() && kb::tests::NearlyEqual(elapsedAfterOneUpdate.Output("elapsed")->AsFloat(), 0.5F), "Time.Elapsed must accumulate the raw deltaSeconds passed to Update()");

    static_cast<void>(scene.Runtime().Update(0.25F));
    const kb::script::ScriptFunctionCallResult elapsedAfterTwoUpdates = host.Functions().Call("Time.Elapsed", {}, context);
    kb::tests::Require(elapsedAfterTwoUpdates.Succeeded() && kb::tests::NearlyEqual(elapsedAfterTwoUpdates.Output("elapsed")->AsFloat(), 0.75F), "Time.Elapsed must keep accumulating across multiple Update() calls");

    const kb::script::ScriptFunctionCallResult timeFrameResult = host.Functions().Call("Time.FrameIndex", {}, context);
    const kb::script::ScriptFunctionCallResult worldFrameResult = host.Functions().Call("World.FrameIndex", {}, context);
    kb::tests::Require(timeFrameResult.Succeeded() && worldFrameResult.Succeeded() && timeFrameResult.Output("frame")->AsInt64() == worldFrameResult.Output("frame")->AsInt64() && timeFrameResult.Output("frame")->AsInt64() == 2,
        "Time.FrameIndex must alias the exact same SceneRuntime counter World.FrameIndex uses, not a separate one (LIB-065 POWRÓT)");

    const kb::script::ScriptFunctionCallResult timeStepResult = host.Functions().Call("Time.FixedStepIndex", {}, context);
    const kb::script::ScriptFunctionCallResult worldStepResult = host.Functions().Call("World.FixedStepIndex", {}, context);
    kb::tests::Require(timeStepResult.Succeeded() && worldStepResult.Succeeded() && timeStepResult.Output("step")->AsInt64() == worldStepResult.Output("step")->AsInt64(),
        "Time.FixedStepIndex must alias the exact same SceneRuntime counter World.FixedStepIndex uses");

    const kb::script::ScriptFunctionCallContext noSceneContext{
        .scene = nullptr,
        .deltaSeconds = 0.1F,
    };
    const kb::script::ScriptFunctionCallResult noSceneDelta = host.Functions().Call("Time.Delta", {}, noSceneContext);
    kb::tests::Require(noSceneDelta.Succeeded(), "Time.Delta must not require a scene");
    const kb::script::ScriptFunctionCallResult noSceneElapsed = host.Functions().Call("Time.Elapsed", {}, noSceneContext);
    kb::tests::Require(!noSceneElapsed.Succeeded(), "Time.Elapsed must fail honestly without a scene rather than silently returning 0");
}

// LIB-094: Time.Scale/Time.SetScale plus the explicit pause rules — Time.Delta
// scaled+zeroed-while-paused, Time.UnscaledDelta always raw, and FixedTick
// genuinely not firing while paused (no catch-up burst on resume).
void RunScriptTimeApiScaleAndPauseTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script time API host did not initialize for scale/pause test");
    kb::tests::Require(host.Functions().FindSignature("Time.Scale") != nullptr, "Time.Scale was not registered");
    kb::tests::Require(host.Functions().FindSignature("Time.SetScale") != nullptr, "Time.SetScale was not registered");

    const kb::script::ScriptFunctionCallContext context{
        .scene = &scene,
        .deltaSeconds = 0.1F,
    };

    const kb::script::ScriptFunctionCallResult defaultScale = host.Functions().Call("Time.Scale", {}, context);
    kb::tests::Require(defaultScale.Succeeded() && kb::tests::NearlyEqual(defaultScale.Output("scale")->AsFloat(), 1.0F), "Time.Scale must default to 1.0 (unscaled)");

    const kb::script::ScriptFunctionCallResult defaultDelta = host.Functions().Call("Time.Delta", {}, context);
    kb::tests::Require(defaultDelta.Succeeded() && kb::tests::NearlyEqual(defaultDelta.Output("delta")->AsFloat(), 0.1F), "Time.Delta must equal raw deltaSeconds at default scale 1.0");

    const std::vector<kb::script::ScriptFunctionArgument> negativeScaleArgs{
        kb::script::ScriptFunctionArgument{ .name = "scale", .value = kb::script::ScriptValue{ -1.0F } },
    };
    const kb::script::ScriptFunctionCallResult rejectedScale = host.Functions().Call("Time.SetScale", negativeScaleArgs, context);
    kb::tests::Require(!rejectedScale.Succeeded(), "Time.SetScale must honestly reject a negative scale rather than silently clamping it");
    const kb::script::ScriptFunctionCallResult scaleStillDefault = host.Functions().Call("Time.Scale", {}, context);
    kb::tests::Require(scaleStillDefault.Succeeded() && kb::tests::NearlyEqual(scaleStillDefault.Output("scale")->AsFloat(), 1.0F), "A rejected Time.SetScale call must not have mutated the stored scale");

    const std::vector<kb::script::ScriptFunctionArgument> doubleScaleArgs{
        kb::script::ScriptFunctionArgument{ .name = "scale", .value = kb::script::ScriptValue{ 2.0F } },
    };
    const kb::script::ScriptFunctionCallResult setDouble = host.Functions().Call("Time.SetScale", doubleScaleArgs, context);
    kb::tests::Require(setDouble.Succeeded() && setDouble.Output("set")->AsBool(), "Time.SetScale(2.0) must succeed");
    const kb::script::ScriptFunctionCallResult scaledDelta = host.Functions().Call("Time.Delta", {}, context);
    kb::tests::Require(scaledDelta.Succeeded() && kb::tests::NearlyEqual(scaledDelta.Output("delta")->AsFloat(), 0.2F), "Time.Delta must reflect Time.Scale (0.1 * 2.0 == 0.2)");
    const kb::script::ScriptFunctionCallResult unscaledStillRaw = host.Functions().Call("Time.UnscaledDelta", {}, context);
    kb::tests::Require(unscaledStillRaw.Succeeded() && kb::tests::NearlyEqual(unscaledStillRaw.Output("delta")->AsFloat(), 0.1F), "Time.UnscaledDelta must stay raw regardless of Time.Scale");

    scene.Runtime().SetPlaying(false);
    const kb::script::ScriptFunctionCallResult pausedDelta = host.Functions().Call("Time.Delta", {}, context);
    kb::tests::Require(pausedDelta.Succeeded() && kb::tests::NearlyEqual(pausedDelta.Output("delta")->AsFloat(), 0.0F), "Time.Delta must read 0 while the scene is paused, regardless of Time.Scale");
    const kb::script::ScriptFunctionCallResult pausedUnscaled = host.Functions().Call("Time.UnscaledDelta", {}, context);
    kb::tests::Require(pausedUnscaled.Succeeded() && kb::tests::NearlyEqual(pausedUnscaled.Output("delta")->AsFloat(), 0.1F), "Time.UnscaledDelta must keep advancing at raw wall-clock rate even while paused");
    scene.Runtime().SetPlaying(true);

    // FixedTick-during-pause: reuse the exact ScriptRuntimeSceneSystem
    // harness RunScriptRuntimeSceneSystemFixedAccumulatorTest above already
    // established (native FixedTick callback counting invocations).
    kb::script::ScriptRuntime fixedTickRuntime;
    kb::scene::Scene fixedTickScene;
    constexpr kb::assets::AssetId kPauseFixedTickAsset{ 1210U };
    const kb::scene::SceneObject fixedTickObject = fixedTickScene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Pause FixedTick Scripted" });
    fixedTickScene.Components().Behaviours().Set(fixedTickObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kPauseFixedTickAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });
    auto pauseNativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    std::size_t pausedFixedTicks = 0U;
    std::vector<float> pausedTickDeltas;
    kb::tests::Require(pauseNativeBackend->RegisterLifecycle(kPauseFixedTickAsset, kb::script::ScriptLifecycleEvent::FixedTick, [&pausedFixedTicks](kb::script::ScriptExecutionContext&) {
                            ++pausedFixedTicks;
                        }),
        "Pause FixedTick callback registration failed");
    kb::tests::Require(pauseNativeBackend->RegisterLifecycle(kPauseFixedTickAsset, kb::script::ScriptLifecycleEvent::Tick, [&pausedTickDeltas](kb::script::ScriptExecutionContext& tickContext) {
                            pausedTickDeltas.push_back(tickContext.DeltaSeconds());
                        }),
        "Pause Tick callback registration failed");
    kb::tests::Require(fixedTickRuntime.RegisterBackend(std::move(pauseNativeBackend)), "Pause FixedTick native backend registration failed");

    kb::script::ScriptRuntimeSceneSystem pauseSystem{ fixedTickRuntime };
    const float fixedStep = pauseSystem.FrameSettings().fixedDeltaSeconds;

    fixedTickScene.Runtime().SetPlaying(false);
    static_cast<void>(pauseSystem.ExecuteFrame(fixedTickScene, fixedStep * 3.0F));
    kb::tests::Require(pausedFixedTicks == 0U, "FixedTick must not fire at all while the scene is paused, no matter how much wall-clock time elapses");
    kb::tests::Require(pausedTickDeltas.size() == 1U, "Tick must keep firing while paused (only FixedTick freezes, not the whole scheduler)");

    fixedTickScene.Runtime().SetPlaying(true);
    static_cast<void>(pauseSystem.ExecuteFrame(fixedTickScene, fixedStep * 0.5F));
    kb::tests::Require(pausedFixedTicks == 0U, "Resuming must not replay time that elapsed while paused as a burst of catch-up FixedTicks");
    static_cast<void>(pauseSystem.ExecuteFrame(fixedTickScene, fixedStep * 0.5F));
    kb::tests::Require(pausedFixedTicks == 1U, "FixedTick must resume firing normally once unpaused");
}

// LIB-095: Timer.Once/Timer.Repeat/Timer.Cancel/Timer.Pause/Timer.Resume
// through the script registry — registration, invalid-input rejection,
// idempotent Cancel, and the capacity-independent id uniqueness.
void RunScriptTimerApiTest() {
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script timer API host did not initialize");
    kb::tests::Require(host.Functions().FindSignature("Timer.Once") != nullptr, "Timer.Once was not registered");
    kb::tests::Require(host.Functions().FindSignature("Timer.Repeat") != nullptr, "Timer.Repeat was not registered");
    kb::tests::Require(host.Functions().FindSignature("Timer.Cancel") != nullptr, "Timer.Cancel was not registered");
    kb::tests::Require(host.Functions().FindSignature("Timer.Pause") != nullptr, "Timer.Pause was not registered");
    kb::tests::Require(host.Functions().FindSignature("Timer.Resume") != nullptr, "Timer.Resume was not registered");

    const kb::script::ScriptFunctionCallContext context{
        .scene = &scene,
        .deltaSeconds = 0.1F,
    };

    const std::vector<kb::script::ScriptFunctionArgument> negativeDelayArgs{
        kb::script::ScriptFunctionArgument{ .name = "delay", .value = kb::script::ScriptValue{ -1.0F } },
    };
    const kb::script::ScriptFunctionCallResult negativeDelay = host.Functions().Call("Timer.Once", negativeDelayArgs, context);
    kb::tests::Require(!negativeDelay.Succeeded(), "Timer.Once must reject a non-positive delay");

    const std::vector<kb::script::ScriptFunctionArgument> zeroDelayArgs{
        kb::script::ScriptFunctionArgument{ .name = "delay", .value = kb::script::ScriptValue{ 0.0F } },
    };
    const kb::script::ScriptFunctionCallResult zeroDelay = host.Functions().Call("Timer.Once", zeroDelayArgs, context);
    kb::tests::Require(!zeroDelay.Succeeded(), "Timer.Once must reject a zero delay");

    const std::vector<kb::script::ScriptFunctionArgument> onceArgs{
        kb::script::ScriptFunctionArgument{ .name = "delay", .value = kb::script::ScriptValue{ 1.0F } },
    };
    const kb::script::ScriptFunctionCallResult onceResult = host.Functions().Call("Timer.Once", onceArgs, context);
    kb::tests::Require(onceResult.Succeeded(), "Timer.Once with a valid delay must succeed");
    const std::uint64_t onceId = onceResult.Output("timer")->AsUInt64();
    kb::tests::Require(onceId != 0U, "Timer.Once must return a non-zero handle on success");

    const std::vector<kb::script::ScriptFunctionArgument> repeatArgs{
        kb::script::ScriptFunctionArgument{ .name = "interval", .value = kb::script::ScriptValue{ 0.5F } },
    };
    const kb::script::ScriptFunctionCallResult repeatResult = host.Functions().Call("Timer.Repeat", repeatArgs, context);
    kb::tests::Require(repeatResult.Succeeded(), "Timer.Repeat with a valid interval must succeed");
    const std::uint64_t repeatId = repeatResult.Output("timer")->AsUInt64();
    kb::tests::Require(repeatId != 0U && repeatId != onceId, "Timer.Repeat must return a distinct handle from Timer.Once's");

    const std::vector<kb::script::ScriptFunctionArgument> cancelOnceArgs{
        kb::script::ScriptFunctionArgument{ .name = "timer", .value = kb::script::ScriptValue{ onceId, kb::script::ScriptValueType::Hash } },
    };
    const kb::script::ScriptFunctionCallResult firstCancel = host.Functions().Call("Timer.Cancel", cancelOnceArgs, context);
    kb::tests::Require(firstCancel.Succeeded() && firstCancel.Output("cancelled")->AsBool(), "Timer.Cancel on a live timer must succeed and report cancelled=true");
    const kb::script::ScriptFunctionCallResult secondCancel = host.Functions().Call("Timer.Cancel", cancelOnceArgs, context);
    kb::tests::Require(secondCancel.Succeeded() && !secondCancel.Output("cancelled")->AsBool(), "Timer.Cancel must be idempotent — a second cancel of an already-cancelled timer must report cancelled=false, not error");

    const std::vector<kb::script::ScriptFunctionArgument> unknownTimerArgs{
        kb::script::ScriptFunctionArgument{ .name = "timer", .value = kb::script::ScriptValue{ std::uint64_t{ 999999U }, kb::script::ScriptValueType::Hash } },
    };
    const kb::script::ScriptFunctionCallResult pauseUnknown = host.Functions().Call("Timer.Pause", unknownTimerArgs, context);
    kb::tests::Require(pauseUnknown.Succeeded() && !pauseUnknown.Output("set")->AsBool(), "Timer.Pause on an unknown handle must honestly report set=false, not error");

    const std::vector<kb::script::ScriptFunctionArgument> pauseRepeatArgs{
        kb::script::ScriptFunctionArgument{ .name = "timer", .value = kb::script::ScriptValue{ repeatId, kb::script::ScriptValueType::Hash } },
    };
    const kb::script::ScriptFunctionCallResult pauseRepeat = host.Functions().Call("Timer.Pause", pauseRepeatArgs, context);
    kb::tests::Require(pauseRepeat.Succeeded() && pauseRepeat.Output("set")->AsBool(), "Timer.Pause on a live timer must succeed");
    const kb::script::ScriptFunctionCallResult pauseRepeatAgain = host.Functions().Call("Timer.Pause", pauseRepeatArgs, context);
    kb::tests::Require(pauseRepeatAgain.Succeeded() && pauseRepeatAgain.Output("set")->AsBool(), "Timer.Pause on an already-paused timer must still report set=true (a 'set' operation, not a 'changed' operation)");
    const kb::script::ScriptFunctionCallResult resumeRepeat = host.Functions().Call("Timer.Resume", pauseRepeatArgs, context);
    kb::tests::Require(resumeRepeat.Succeeded() && resumeRepeat.Output("set")->AsBool(), "Timer.Resume on a live timer must succeed");

    const kb::script::ScriptFunctionCallContext noSceneContext{
        .scene = nullptr,
        .deltaSeconds = 0.1F,
    };
    const kb::script::ScriptFunctionCallResult noSceneOnce = host.Functions().Call("Timer.Once", onceArgs, noSceneContext);
    kb::tests::Require(!noSceneOnce.Succeeded(), "Timer.Once must fail honestly without a scene rather than silently returning a handle");
}

// LIB-095: end-to-end firing behavior through the real ScriptRuntimeSceneSystem
// per-frame drive — reuses the exact harness shape RunScriptRuntimeSceneSystemFixedAccumulatorTest
// established (native ExecuteFrame calls with controlled deltaSeconds).
// Exercises the parts Timer.Once/Repeat's own script-facing test above
// cannot: owner-targeted vs. no-owner-broadcast dispatch, scene-pause
// freezing a timer's countdown, per-timer Pause/Resume freezing exactly,
// dead-owner auto-cancellation, and Timer.Repeat firing more than once.
void RunScriptTimerApiFiringOwnerAndPauseTest() {
    kb::script::ScriptRuntime runtime;
    kb::scene::Scene scene;
    constexpr kb::assets::AssetId kOwnerAsset{ 1220U };
    constexpr kb::assets::AssetId kOtherAsset{ 1221U };
    const kb::scene::SceneObject ownerObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Timer Owner" });
    const kb::scene::SceneObject otherObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Timer Other" });
    scene.Components().Behaviours().Set(ownerObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kOwnerAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });
    scene.Components().Behaviours().Set(otherObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = kOtherAsset.value,
        .backend = kb::scene::BehaviourBackend::Native,
        .enabled = true,
    });

    auto nativeBackend = std::make_unique<kb::script::NativeScriptBackend>();
    std::size_t ownerFired = 0U;
    std::size_t otherFired = 0U;
    kb::tests::Require(nativeBackend->RegisterEvent(kOwnerAsset, "TimerFired", [&ownerFired](kb::script::ScriptExecutionContext&, const kb::script::ScriptEvent&) {
                            ++ownerFired;
                        }),
        "Timer owner TimerFired listener registration failed");
    kb::tests::Require(nativeBackend->RegisterEvent(kOtherAsset, "TimerFired", [&otherFired](kb::script::ScriptExecutionContext&, const kb::script::ScriptEvent&) {
                            ++otherFired;
                        }),
        "Timer other TimerFired listener registration failed");
    kb::tests::Require(runtime.RegisterBackend(std::move(nativeBackend)), "Timer native backend registration failed");

    kb::script::ScriptRuntimeSceneSystem system{ runtime };

    // (A) no owner => broadcast reaches every enabled behaviour.
    const std::uint64_t broadcastId = scene.Timers().Once(0.05F, kb::scene::SceneEntity{});
    kb::tests::Require(broadcastId != 0U, "SceneTimers::Once with no owner must still succeed");
    static_cast<void>(system.ExecuteFrame(scene, 0.05F));
    kb::tests::Require(ownerFired == 1U && otherFired == 1U, "A no-owner timer must broadcast TimerFired to every enabled behaviour");

    // (B) explicit owner => targeted dispatch reaches ONLY that entity.
    const std::uint64_t targetedId = scene.Timers().Once(0.05F, ownerObject.Entity());
    kb::tests::Require(targetedId != 0U, "SceneTimers::Once with an owner must succeed");
    static_cast<void>(system.ExecuteFrame(scene, 0.05F));
    kb::tests::Require(ownerFired == 2U && otherFired == 1U, "An owned timer must target ONLY its owner's behaviour, not broadcast");

    // (C) scene-level pause freezes the countdown entirely — no matter how
    // much wall-clock time elapses while paused, the timer does not fire,
    // and no debt is replayed as a burst once resumed.
    const std::uint64_t pausedSceneId = scene.Timers().Once(0.1F, ownerObject.Entity());
    scene.Runtime().SetPlaying(false);
    static_cast<void>(system.ExecuteFrame(scene, 10.0F));
    kb::tests::Require(ownerFired == 2U, "Timer.Once must not fire while the whole scene is paused, regardless of elapsed wall-clock time");
    scene.Runtime().SetPlaying(true);
    static_cast<void>(system.ExecuteFrame(scene, 0.1F));
    kb::tests::Require(ownerFired == 3U, "Timer.Once must resume counting down normally once the scene is unpaused, with no catch-up burst");
    static_cast<void>(pausedSceneId);

    // (D) per-timer Pause/Resume freezes remaining time exactly (distinct
    // from scene-level pause above — this is Timer.Pause/Resume itself).
    const std::uint64_t individuallyPausedId = scene.Timers().Once(0.1F, ownerObject.Entity());
    kb::tests::Require(scene.Timers().Pause(individuallyPausedId), "SceneTimers::Pause on a live timer must succeed");
    static_cast<void>(system.ExecuteFrame(scene, 5.0F));
    kb::tests::Require(ownerFired == 3U, "An individually-paused timer must not fire no matter how much time elapses");
    kb::tests::Require(scene.Timers().Resume(individuallyPausedId), "SceneTimers::Resume on a live timer must succeed");
    static_cast<void>(system.ExecuteFrame(scene, 0.1F));
    kb::tests::Require(ownerFired == 4U, "Resuming an individually-paused timer must let it fire normally afterward");

    // (E) dead owner => silently auto-cancelled, never fires, no crash.
    const kb::scene::SceneObject doomedObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Timer Doomed Owner" });
    const std::uint64_t doomedId = scene.Timers().Once(1.0F, doomedObject.Entity());
    kb::tests::Require(scene.Timers().Exists(doomedId), "A freshly created timer must exist immediately");
    scene.Entities().Destroy(doomedObject);
    static_cast<void>(system.ExecuteFrame(scene, 1.0F));
    kb::tests::Require(ownerFired == 4U && otherFired == 1U, "A timer whose owner died must never fire (no dangling callback for a dead entity)");
    kb::tests::Require(!scene.Timers().Exists(doomedId), "A dead-owner timer must be auto-cancelled (removed), not left dangling");

    // (F) Timer.Repeat fires more than once, resetting its own countdown
    // each time (flat reset — no overshoot compensation, LIB-096's job).
    const std::uint64_t repeatId = scene.Timers().Repeat(0.1F, ownerObject.Entity());
    kb::tests::Require(repeatId != 0U, "SceneTimers::Repeat must succeed");
    static_cast<void>(system.ExecuteFrame(scene, 0.1F));
    kb::tests::Require(ownerFired == 5U, "Timer.Repeat must fire on its first interval");
    static_cast<void>(system.ExecuteFrame(scene, 0.1F));
    kb::tests::Require(ownerFired == 6U, "Timer.Repeat must fire AGAIN on its second interval, proving it resets rather than being removed after firing once");
    kb::tests::Require(scene.Timers().Exists(repeatId), "A repeating timer must remain alive after firing, unlike a one-shot timer");
    kb::tests::Require(scene.Timers().Cancel(repeatId), "SceneTimers::Cancel must be able to stop a still-alive repeating timer");
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
    RunPendingCommandCancelledByDestroyTest();
    RunTargetedEventDispatchTest();
    RunCrossBackendSharedStateTest();
    RunScriptFunctionRegistryCrossBackendTest();
    RunVisualGraphCallNativeFailureBranchTest();
    RunLuaCallFunctionResultAdapterTest();
    RunScriptFunctionRegistryExceptionSafetyTest();
    RunNativeScriptBackendExceptionSafetyTest();
    RunUnexposedFunctionCannotBeCalledTest();
    RunScriptFunctionRegistryLocksAfterFirstDispatchTest();
    RunScriptFunctionRegistryReentrancyGuardTest();
    RunNativeFullLifecycleOrderTest();
    RunPucLuaFullLifecycleOrderTest();
    RunVisualGraphFullLifecycleOrderTest();
    RunCrossBackendLifecycleOrderParityTest();
    RunScriptAudioApiTest();
    RunScriptWorldTimePhysicsApiTest();
    RunScriptTimeApiElapsedAndAliasingTest();
    RunScriptTimeApiScaleAndPauseTest();
    RunScriptTimerApiTest();
    RunScriptTimerApiFiringOwnerAndPauseTest();
    RunTransformApiLocalAndWorldPoseTest();
    RunTransformApiParentAndHierarchyTest();
    RunTransformApiChildIterationTest();
    RunTransformApiRotateLookAtAndPointConversionTest();
    RunTransformHierarchyEdgeCaseTest();
    RunWorldDestroyDeferredFlagTest();
    RunWorldActiveStateTest();
    RunWorldFindAllByTagTest();
    RunWorldSetPropertyTest();
    RunWorldInstantiatePrefabOwnershipTest();
    RunSceneLoadedContentApiTest();
    RunWorldPersistentStateTest();
    RunScenePersistentEntitySurvivesLoadTest();
    RunSceneLifecycleEventsReachBehaviourTest();
    RunScriptMathApiTest();
    RunMathFunctionCrossBackendParityTest();
    RunScriptInputApiTest();
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
    RunScriptSceneComponentGeneratedAccessorCoverageTest();
    RunScriptSceneComponentPropertiesNeverExposeRawPointerTest();
    RunVisualGraphSceneComponentBindingTest();
}

} // namespace kb::tests
