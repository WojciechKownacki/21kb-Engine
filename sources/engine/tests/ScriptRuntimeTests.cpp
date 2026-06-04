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
#include "engine/script/ScriptRuntimeSceneSystem.hpp"
#include "engine/script/ScriptSceneComponentApi.hpp"
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
}

} // namespace

namespace kb::tests {

void RunScriptRuntimeTests() {
    RunNativeScriptRuntimeDispatchTest();
    RunLuaScriptRuntimeDispatchTest();
    RunPucLuaScriptRuntimeDispatchTest();
    RunCrossBackendEventDispatchTest();
    RunScriptRuntimeSceneSystemTest();
    RunVisualGraphScriptBackendDispatchTest();
    RunScriptRuntimeAssetPreparerEndToEndTest();
    RunScriptRuntimeSceneSystemAssetPreparationTest();
    RunScriptSceneComponentApiTest();
}

} // namespace kb::tests
