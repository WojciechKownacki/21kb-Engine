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
#include "engine/script/NativeScriptBackend.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptBehaviourAsset.hpp"
#include "engine/script/ScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeAssetPreparer.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptSceneVisualGraphBindings.hpp"
#include "engine/script/VisualGraphScriptBackend.hpp"
#include "engine/visual/VisualGraphCompiler.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

class FakeLuaRuntime final : public kb::script::ILuaScriptRuntime {
public:
    bool emitLifecycleEvent = true;
    kb::scene::SceneEntity lifecycleSelf{};
    kb::scene::SceneEntity eventSelf{};
    float lifecycleDelta = 0.0F;
    std::string receivedEventName;
    std::size_t receivedArgumentCount = 0;

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
    if os ~= nil or io ~= nil or package ~= nil or debug ~= nil or dofile ~= nil or loadfile ~= nil then
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

void RunScriptRuntimeAssetPreparerEndToEndTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "Project";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "Player.lua", R"(
function Tick(self, dt)
    Emit("LuaAssetTicked", { entity = self.entity, delta = dt })
end
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
    kb::tests::Require(scene.Assets().Discover() == 2U, "Script runtime asset preparer discovery did not find Lua and graph assets");

    const kb::assets::AssetMetadata* luaMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Player.lua");
    const kb::assets::AssetMetadata* graphMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Enemy.kbgraph");
    kb::tests::Require(luaMetadata != nullptr, "Script runtime asset preparer could not find Lua metadata");
    kb::tests::Require(graphMetadata != nullptr, "Script runtime asset preparer could not find graph metadata");

    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Lua Asset Object" });
    const kb::scene::SceneObject graphObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Graph Asset Object" });
    const std::optional<kb::scene::BehaviourComponent> luaBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*luaMetadata);
    const std::optional<kb::scene::BehaviourComponent> graphBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*graphMetadata);
    kb::tests::Require(luaBehaviour.has_value(), "Script runtime asset preparer could not create Lua behaviour component");
    kb::tests::Require(graphBehaviour.has_value(), "Script runtime asset preparer could not create graph behaviour component");
    scene.Components().Behaviours().Set(luaObject.Entity(), *luaBehaviour);
    scene.Components().Behaviours().Set(graphObject.Entity(), *graphBehaviour);

    kb::script::PucLuaScriptRuntime luaRuntime;
    kb::visual::VisualGraphRuntimeRegistry visualArtifacts;
    kb::script::ScriptRuntimeAssetPreparer preparer{ scene.Assets().Manager(), luaRuntime, visualArtifacts };
    const kb::script::ScriptRuntimeAssetPrepareResult prepared = preparer.PrepareSceneBehaviours(scene);
    kb::tests::Require(prepared.Succeeded(), "Script runtime asset preparer produced diagnostics");
    kb::tests::Require(prepared.visitedAssets == 2U, "Script runtime asset preparer did not visit both behaviour assets");
    kb::tests::Require(prepared.preparedAssets == 2U, "Script runtime asset preparer did not prepare both behaviour assets");
    kb::tests::Require(luaRuntime.HasScript(luaMetadata->id), "Script runtime asset preparer did not load Lua script into VM");
    kb::tests::Require(visualArtifacts.Contains(graphMetadata->id), "Script runtime asset preparer did not compile graph into runtime registry");

    kb::visual::VisualGraphRuntimeBindingRegistry visualBindings;
    kb::visual::VisualGraphBehaviourInstanceRegistry visualInstances;
    kb::script::ScriptRuntime runtime;
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::LuaScriptBackend>(luaRuntime)), "Script runtime asset preparer Lua backend registration failed");
    kb::tests::Require(runtime.RegisterBackend(std::make_unique<kb::script::VisualGraphScriptBackend>(visualArtifacts, visualBindings, visualInstances)), "Script runtime asset preparer visual backend registration failed");

    const kb::script::ScriptRuntimeExecutionResult tick = runtime.ExecuteLifecycle(scene, kb::script::ScriptLifecycleEvent::Tick, 0.5F);
    kb::tests::Require(tick.Succeeded(), "Prepared script asset runtime Tick produced diagnostics");
    kb::tests::Require(tick.executedBehaviours == 2U, "Prepared script asset runtime did not execute Lua and graph behaviours");

    bool sawLuaEvent = false;
    bool sawGraphEvent = false;
    for (const kb::script::ScriptEvent& event : tick.emittedEvents) {
        sawLuaEvent = sawLuaEvent || event.name == "LuaAssetTicked";
        sawGraphEvent = sawGraphEvent || event.name == "GraphAssetTicked";
    }
    kb::tests::Require(sawLuaEvent, "Prepared script asset runtime did not emit Lua event");
    kb::tests::Require(sawGraphEvent, "Prepared script asset runtime did not emit graph event");
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
    const kb::visual::VisualGraphNodeCatalog catalog = host.CreateVisualGraphNodeCatalog();
    kb::tests::Require(
        catalog.Find("RuntimeBinding:SetProperty:Self.SetProperty.Bool") != nullptr,
        "Script runtime host visual graph catalog did not expose scene property bindings");
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
}

void RunScriptRuntimeHostNativeDescriptorBindingTest() {
    ResetTestRoot();

    const std::filesystem::path projectRoot = TestRoot() / "HostNativeProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "HostNative.native", R"(
name Host Native
symbol HostNative
)");

    kb::scene::Scene scene;
    kb::tests::Require(scene.Assets().MountProject(projectRoot), "Script runtime host native project mount failed");
    kb::tests::Require(scene.Assets().Discover() == 1U, "Script runtime host native asset discovery failed");

    const kb::assets::AssetMetadata* nativeMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/HostNative.native");
    kb::tests::Require(nativeMetadata != nullptr, "Script runtime host native metadata was not discovered");

    kb::script::ScriptRuntimeHost host{ scene };
    kb::tests::Require(host.Succeeded(), "Script runtime host native setup failed");
    kb::tests::Require(host.NativeBackend().RegisterLifecycleSymbol("HostNative", kb::script::ScriptLifecycleEvent::Tick, [](kb::script::ScriptExecutionContext& context) {
                           kb::scene::TransformComponent transform = context.GetScene().Transforms().Get(context.Self());
                           transform.localPosition.x = 6.0F;
                           context.GetScene().Transforms().Set(context.Self(), transform);
                       }),
        "Script runtime host native symbol callback registration failed");

    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Host Native" });
    const std::optional<kb::scene::BehaviourComponent> nativeBehaviour = kb::script::ScriptBehaviourAsset::CreateComponent(*nativeMetadata);
    kb::tests::Require(nativeBehaviour.has_value(), "Script runtime host could not create native behaviour component");
    scene.Components().Behaviours().Set(object.Entity(), *nativeBehaviour);

    kb::tests::Require(host.InstallSceneSystem(), "Script runtime host did not install native scene system");
    static_cast<void>(scene.Runtime().Update(0.016F));

    kb::tests::Require(kb::tests::NearlyEqual(scene.Transforms().Get(object.Entity()).localPosition.x, 6.0F), "Script runtime host did not bind native descriptor symbol");
}

void RunScriptSceneComponentApiTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Script Api Object" });
    scene.Components().Cameras().Set(object.Entity(), kb::scene::CameraComponent{});

    kb::tests::Require(kb::script::ScriptSceneComponentApi::HasComponent(scene, object.Entity(), "Transform"), "Script component API did not see Transform");
    kb::tests::Require(kb::script::ScriptSceneComponentApi::HasComponent(scene, object.Entity(), "Visibility"), "Script component API did not see Visibility");
    kb::tests::Require(kb::script::ScriptSceneComponentApi::HasComponent(scene, object.Entity(), "Camera"), "Script component API did not see Camera");
    kb::tests::Require(!kb::script::ScriptSceneComponentApi::HasComponent(scene, object.Entity(), "Light"), "Script component API reported missing Light as present");

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
    RunScriptRuntimeExecutionOrderTest();
    RunLuaScriptRuntimeDispatchTest();
    RunPucLuaScriptRuntimeDispatchTest();
    RunCrossBackendEventDispatchTest();
    RunScriptRuntimeSceneSystemTest();
    RunVisualGraphScriptBackendDispatchTest();
    RunScriptRuntimeAssetPreparerEndToEndTest();
    RunScriptRuntimeSceneSystemAssetPreparationTest();
    RunScriptRuntimeHostBackendRegistrationTest();
    RunScriptRuntimeHostSceneSystemTest();
    RunScriptRuntimeHostNativeDescriptorBindingTest();
    RunScriptSceneComponentApiTest();
    RunVisualGraphSceneComponentBindingTest();
}

} // namespace kb::tests
