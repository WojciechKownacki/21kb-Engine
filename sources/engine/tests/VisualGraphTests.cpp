#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/script/ScriptApiNameCollector.hpp"
#include "engine/script/ScriptApiNameRegistry.hpp"
#include "engine/script/ScriptAssetLoader.hpp"
#include "engine/visual/VisualGraphAssetLoader.hpp"
#include "engine/visual/VisualGraphAssetWriter.hpp"
#include "engine/visual/VisualGraphBehaviourInstanceRegistry.hpp"
#include "engine/visual/VisualGraphBehaviourLifecycleRunner.hpp"
#include "engine/visual/VisualGraphBehaviourRuntime.hpp"
#include "engine/visual/VisualGraphBuildPipeline.hpp"
#include "engine/visual/VisualGraphCompileService.hpp"
#include "engine/visual/VisualGraphCompiler.hpp"
#include "engine/visual/VisualGraphCompileCoordinator.hpp"
#include "engine/visual/VisualGraphDocument.hpp"
#include "engine/visual/VisualGraphNativeBindingRegistry.hpp"
#include "engine/visual/VisualGraphNativeCodeGenerator.hpp"
#include "engine/visual/VisualGraphNodeCatalog.hpp"
#include "engine/visual/VisualGraphNodeDefinitionRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeExecutionContext.hpp"
#include "engine/visual/VisualGraphRuntimeExecutor.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_visual_graph_tests";
}

void ResetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot(), error);
    kb::tests::Require(!error, "Visual graph test root could not be prepared");
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    kb::tests::Require(!error, "Visual graph test directory could not be created");

    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    kb::tests::Require(output.is_open(), "Visual graph test file could not be opened");
    output << text;
    kb::tests::Require(output.good(), "Visual graph test file could not be written");
}

[[nodiscard]] std::string SampleGraphText() {
    return R"(kbgraph 1
name PlayerController
variable speed Float 6.0
node 1 Event Ready
pin 1 Output then Void
node 2 GetComponent TransformComponent
pin 2 Input exec Void
pin 2 Output then Void
node 3 CallNative CacheTransform
pin 3 Input exec Void
edge exec 1 then 2 exec
edge exec 2 then 3 exec
node 10 Event Tick
pin 10 Output then Void
node 20 GetProperty DeltaSeconds
pin 20 Output value Float
node 11 CallNative MovePlayer
pin 11 Input exec Void
pin 11 Input delta Float
pin 11 Output then Void
node 12 EmitEvent PlayerMoved
pin 12 Input exec Void
edge exec 10 then 11 exec
edge exec 11 then 12 exec
edge data 20 value 11 delta
)";
}

[[nodiscard]] std::string SampleCustomEventGraphText() {
    return R"(kbgraph 1
name DoorListener
node 1 CustomEvent DoorOpened
pin 1 Output then Void
pin 1 Output door Entity
node 2 CallNative HandleDoor
pin 2 Input exec Void
pin 2 Input door Entity
pin 2 Output then Void
edge exec 1 then 2 exec
edge data 1 door 2 door
)";
}

[[nodiscard]] std::string FunctionSignatureGraphText() {
    return R"(kbgraph 1
name InventoryCaller
node 1 Event Tick
pin 1 Output then Void
node 2 CallNative Function.Inventory.AddItem
pin 2 Input exec Void
pin 2 Input itemId String
pin 2 Output then Void
pin 2 Output total Int
edge exec 1 then 2 exec
)";
}

void RunVisualGraphAssetLoaderTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "Project";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "PlayerController.kbgraph", SampleGraphText());

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>()), "Visual graph loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Visual graph asset mount failed");
    kb::tests::Require(manager.DiscoverMountedAssets() == 1U, "Visual graph discovery did not find the graph asset");

    const kb::assets::AssetHandle<kb::visual::VisualGraphAsset> graph = manager.Load<kb::visual::VisualGraphAsset>("/Game/Logic/PlayerController.kbgraph");
    kb::tests::Require(graph.IsLoaded(), "Visual graph asset did not load");
    kb::tests::Require(graph->name == "PlayerController", "Visual graph asset name was not parsed");
    kb::tests::Require(graph->variables.size() == 1U, "Visual graph variables were not parsed");
    kb::tests::Require(graph->nodes.size() == 7U, "Visual graph nodes were not parsed");
    kb::tests::Require(graph->pins.size() == 10U, "Visual graph pins were not parsed");
    kb::tests::Require(graph->edges.size() == 5U, "Visual graph edges were not parsed");

    const std::string serialized = kb::visual::VisualGraphAssetWriter::WriteToString(*graph.Get());
    kb::tests::Require(serialized.find("edge Execution 10 then 11 exec") != std::string::npos, "Visual graph writer did not preserve typed execution edges");
    kb::tests::Require(serialized.find("edge Data 20 value 11 delta") != std::string::npos, "Visual graph writer did not preserve typed data edges");
}

void RunVisualGraphCustomEventEntryTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "Project";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "DoorListener.kbgraph", SampleCustomEventGraphText());

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>()), "Visual graph custom event loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Visual graph custom event asset mount failed");
    static_cast<void>(manager.DiscoverMountedAssets());

    const kb::assets::AssetHandle<kb::visual::VisualGraphAsset> graph = manager.Load<kb::visual::VisualGraphAsset>("/Game/Logic/DoorListener.kbgraph");
    kb::tests::Require(graph.IsLoaded(), "Visual graph custom event asset did not load");
    kb::tests::Require(graph->nodes.size() == 2U && graph->nodes[0].kind == kb::visual::VisualGraphNodeKind::CustomEvent,
        "Visual graph custom event node was not parsed");
    kb::tests::Require(kb::visual::VisualGraphAssetWriter::WriteToString(*graph.Get()).find("node 1 CustomEvent DoorOpened") != std::string::npos,
        "Visual graph writer did not preserve custom event nodes");

    kb::visual::VisualGraphRuntimeRegistry runtimeRegistry;
    const kb::visual::VisualGraphCompileServiceResult compiled = kb::visual::VisualGraphCompileService::CompileAsset(
        manager,
        graph.Id(),
        kb::visual::VisualGraphCompileRequest{
            .build = kb::visual::VisualGraphBuildDesc{
                .nativeCodegen = kb::visual::VisualGraphNativeCodegenDesc{
                    .className = "DoorListener",
                    .namespaceName = "kb::game",
                },
            },
            .writeGeneratedCode = false,
        },
        runtimeRegistry);
    kb::tests::Require(compiled.Succeeded(), "Visual graph custom event graph did not compile");
    kb::tests::Require(compiled.artifact.module.functions.size() == 1U, "Visual graph custom event compiler produced the wrong function count");
    kb::tests::Require(compiled.artifact.module.functions[0].customEventName == "DoorOpened", "Visual graph compiler did not preserve custom event name");
    kb::tests::Require(compiled.artifact.nativeCode.header.find("void Event_DoorOpened") != std::string::npos,
        "Visual graph native codegen did not expose a callable custom event entry");

    kb::scene::Scene scene;
    const kb::scene::SceneObject door = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Door"});
    scene.Components().Behaviours().Set(door.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = graph.Id().value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    });

    std::vector<std::uint64_t> handledEntities;
    std::vector<std::uint64_t> handledDoorArguments;
    kb::visual::VisualGraphRuntimeBindingRegistry runtimeBindings;
    kb::tests::Require(runtimeBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "HandleDoor",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "door", .type = kb::visual::VisualGraphValueType::Entity},
                           },
                           .callback = [&handledEntities, &handledDoorArguments](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               handledEntities.push_back(context.ReadUInt64(0U, "self"));
                               handledDoorArguments.push_back(context.ReadUInt64(instruction.inputs[0].sourceNodeId, instruction.inputs[0].sourcePin));
                           },
                       }),
        "Visual graph custom event runtime binding registration failed");

    kb::visual::VisualGraphBehaviourInstanceRegistry instances;
    const std::vector<kb::visual::VisualGraphCustomEventArgument> arguments{
        kb::visual::VisualGraphCustomEventArgument{
            .name = "door",
            .value = kb::visual::VisualGraphRuntimeValue{door.Entity().Id(), kb::visual::VisualGraphValueType::Entity},
        },
    };
    const kb::visual::VisualGraphBehaviourLifecycleResult dispatched = kb::visual::VisualGraphBehaviourLifecycleRunner::ExecuteCustomEvent(
        scene,
        "DoorOpened",
        arguments,
        runtimeRegistry,
        runtimeBindings,
        instances);
    kb::tests::Require(dispatched.Succeeded(), "Visual graph custom event dispatch failed");
    kb::tests::Require(dispatched.visitedBehaviours == 1U, "Visual graph custom event dispatch did not visit the behaviour component");
    kb::tests::Require(dispatched.executedBehaviours == 1U, "Visual graph custom event dispatch did not execute the matching event entry");
    kb::tests::Require(handledEntities.size() == 1U && handledEntities[0] == door.Entity().Id(), "Visual graph custom event dispatch did not preserve self");
    kb::tests::Require(handledDoorArguments.size() == 1U && handledDoorArguments[0] == door.Entity().Id(), "Visual graph custom event dispatch did not pass typed payload");

    handledEntities.clear();
    handledDoorArguments.clear();
    const kb::visual::VisualGraphBehaviourLifecycleResult missing = kb::visual::VisualGraphBehaviourLifecycleRunner::ExecuteCustomEvent(
        scene,
        "MissingEvent",
        arguments,
        runtimeRegistry,
        runtimeBindings,
        instances);
    kb::tests::Require(missing.Succeeded(), "Visual graph custom event dispatch failed for an unhandled event");
    kb::tests::Require(missing.executedBehaviours == 0U, "Visual graph custom event dispatch executed a missing event entry");
    kb::tests::Require(handledEntities.empty(), "Visual graph custom event dispatch called bindings for a missing event");

    const kb::visual::VisualGraphBehaviourLifecycleResult invalidPayload = kb::visual::VisualGraphBehaviourLifecycleRunner::ExecuteCustomEvent(
        scene,
        "DoorOpened",
        std::span<const kb::visual::VisualGraphCustomEventArgument>{},
        runtimeRegistry,
        runtimeBindings,
        instances);
    kb::tests::Require(!invalidPayload.Succeeded(), "Visual graph custom event dispatch accepted a missing required payload");
}

void RunVisualGraphCompilerAndCodegenTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "Project";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "PlayerController.kbgraph", SampleGraphText());

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>()), "Visual graph loader registration failed for compiler test");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Visual graph compiler test asset mount failed");
    static_cast<void>(manager.DiscoverMountedAssets());

    const kb::assets::AssetHandle<kb::visual::VisualGraphAsset> graph = manager.Load<kb::visual::VisualGraphAsset>("/Game/Logic/PlayerController.kbgraph");
    kb::tests::Require(graph.IsLoaded(), "Visual graph compiler test asset did not load");

    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(*graph.Get());
    kb::tests::Require(compiled.Succeeded(), "Visual graph compiler rejected a valid graph");
    kb::tests::Require(compiled.module.functions.size() == 2U, "Visual graph compiler did not produce lifecycle entry points");
    kb::tests::Require(compiled.module.functions[0].event == kb::visual::VisualGraphLifecycleEvent::Ready, "Visual graph compiler produced wrong first lifecycle entry");
    kb::tests::Require(compiled.module.functions[0].instructions.size() == 2U, "Visual graph compiler did not compile Ready chain");
    kb::tests::Require(compiled.module.functions[1].event == kb::visual::VisualGraphLifecycleEvent::Tick, "Visual graph compiler produced wrong second lifecycle entry");
    kb::tests::Require(compiled.module.functions[1].instructions.size() == 3U, "Visual graph compiler did not compile Tick data dependencies");
    kb::tests::Require(compiled.module.functions[1].instructions[0].outputs.size() == 1U, "Visual graph compiler did not attach data outputs");
    kb::tests::Require(compiled.module.functions[1].instructions[0].outputs[0].name == "value", "Visual graph compiler attached the wrong data output");
    kb::tests::Require(compiled.module.functions[1].instructions[1].inputs.size() == 1U, "Visual graph compiler did not attach data inputs");
    kb::tests::Require(compiled.module.functions[1].instructions[1].inputs[0].name == "delta", "Visual graph compiler attached the wrong data input");

    const kb::visual::VisualGraphNativeCode generated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "PlayerController",
        .namespaceName = "kb::game",
    });
    kb::tests::Require(generated.header.find("class PlayerController final") != std::string::npos, "Visual graph native codegen did not emit the class");
    kb::tests::Require(generated.headerFileName == "PlayerController.generated.hpp", "Visual graph native codegen did not expose the generated header file name");
    kb::tests::Require(generated.sourceFileName == "PlayerController.generated.cpp", "Visual graph native codegen did not expose the generated source file name");
    kb::tests::Require(generated.header.find("void Ready") != std::string::npos, "Visual graph native codegen did not emit Ready");
    kb::tests::Require(generated.source.find("context.CallNative(\"MovePlayer\")") != std::string::npos, "Visual graph native codegen did not emit native call dispatch");
    kb::tests::Require(generated.source.find("context.EmitEvent(\"PlayerMoved\")") != std::string::npos, "Visual graph native codegen did not emit event dispatch");

    kb::visual::VisualGraphAsset payloadGraph{};
    payloadGraph.name = "PayloadEmitter";
    payloadGraph.nodes = {
        kb::visual::VisualGraphNode{.id = 1U, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick},
        kb::visual::VisualGraphNode{.id = 2U, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "PayloadAmount"},
        kb::visual::VisualGraphNode{.id = 3U, .kind = kb::visual::VisualGraphNodeKind::EmitEvent, .symbol = "PayloadReady"},
    };
    payloadGraph.pins = {
        kb::visual::VisualGraphPin{.nodeId = 1U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 2U, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "amount", .type = kb::visual::VisualGraphValueType::Float},
        kb::visual::VisualGraphPin{.nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 3U, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "amount", .type = kb::visual::VisualGraphValueType::Float},
    };
    payloadGraph.edges = {
        kb::visual::VisualGraphEdge{.fromNode = 1U, .fromPin = "then", .toNode = 3U, .toPin = "exec"},
        kb::visual::VisualGraphEdge{.fromNode = 2U, .fromPin = "amount", .toNode = 3U, .toPin = "amount", .kind = kb::visual::VisualGraphEdgeKind::Data},
    };
    const kb::visual::VisualGraphCompileResult payloadCompiled = kb::visual::VisualGraphCompiler::Compile(payloadGraph);
    kb::tests::Require(payloadCompiled.Succeeded(), "Visual graph compiler rejected an event payload graph");
    const kb::visual::VisualGraphNativeCode payloadGenerated = kb::visual::VisualGraphNativeCodeGenerator::Generate(payloadCompiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "PayloadEmitter",
        .namespaceName = "kb::game",
    });
    kb::tests::Require(payloadGenerated.Succeeded(), "Visual graph native codegen rejected an event payload graph");
    kb::tests::Require(payloadGenerated.header.find("std::span<const VisualGraphNativeEventArgument>") != std::string::npos,
        "Visual graph native codegen did not expose typed event payloads");
    kb::tests::Require(payloadGenerated.source.find("VisualGraphNativeEventArgument{\"amount\", VisualGraphNativeValueType::Float, context.ReadFloat(2U, \"amount\")}") != std::string::npos,
        "Visual graph native codegen did not preserve typed event payload arguments");
    kb::tests::Require(payloadGenerated.source.find("context.EmitEvent(\"PayloadReady\", std::span<const VisualGraphNativeEventArgument>{eventArguments_3})") != std::string::npos,
        "Visual graph native codegen did not emit typed event payload dispatch");

    const kb::visual::VisualGraphNativeCode keywordClassGenerated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "class",
        .namespaceName = "",
    });
    kb::tests::Require(keywordClassGenerated.Succeeded(), "Visual graph native codegen rejected a class name that can be sanitized");
    kb::tests::Require(keywordClassGenerated.header.find("class class_ final") != std::string::npos, "Visual graph native codegen did not sanitize a C++ keyword class name");

    const kb::visual::VisualGraphNativeCode invalidNamespaceGenerated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "PlayerController",
        .namespaceName = "bad namespace",
    });
    kb::tests::Require(!invalidNamespaceGenerated.Succeeded(), "Visual graph native codegen accepted an invalid C++ namespace");

    kb::visual::VisualGraphNativeBindingRegistry bindings;
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "DeltaSeconds",
                           .functionName = "NativeDeltaSeconds",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{
                                   .name = "value",
                                   .type = kb::visual::VisualGraphValueType::Float,
                               },
                           },
                       }),
        "Visual graph native output binding registration failed");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "MovePlayer",
                           .functionName = "NativeMovePlayer",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{
                                   .name = "delta",
                                   .type = kb::visual::VisualGraphValueType::Float,
                               },
                           },
                       }),
        "Visual graph native binding registration failed");

    const kb::visual::VisualGraphNativeCode directGenerated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "PlayerController",
        .namespaceName = "kb::game",
        .sourceIncludes = {"Game/PlayerBindings.hpp"},
        .bindings = &bindings,
    });
    kb::tests::Require(directGenerated.Succeeded(), "Visual graph native codegen rejected a valid direct native binding");
    kb::tests::Require(directGenerated.source.find("#include \"Game/PlayerBindings.hpp\"") != std::string::npos,
        "Visual graph native codegen did not emit source includes for native bindings");
    kb::tests::Require(directGenerated.source.find("context.StoreFloat(20U, \"value\", NativeDeltaSeconds(context));") != std::string::npos,
        "Visual graph native codegen did not emit direct native output binding");
    kb::tests::Require(directGenerated.source.find("NativeMovePlayer(context, context.ReadFloat(20U, \"value\"));") != std::string::npos,
        "Visual graph native codegen did not emit direct native binding with typed inputs");
    kb::tests::Require(directGenerated.source.find("context.CallNative(\"MovePlayer\")") == std::string::npos, "Visual graph native codegen used dispatch despite a direct binding");

    const kb::visual::VisualGraphNativeCode invalidIncludeGenerated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "PlayerController",
        .namespaceName = "kb::game",
        .sourceIncludes = {"Bad\"Include.hpp"},
        .bindings = &bindings,
    });
    kb::tests::Require(!invalidIncludeGenerated.Succeeded(), "Visual graph native codegen accepted an unsafe include path");

    const kb::visual::VisualGraphNativeCode strictPartialGenerated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "PlayerController",
        .namespaceName = "kb::game",
        .bindings = &bindings,
        .requireNativeBindings = true,
    });
    kb::tests::Require(!strictPartialGenerated.Succeeded(), "Visual graph native codegen accepted strict native mode with missing bindings");
    kb::tests::Require(!strictPartialGenerated.diagnostics.empty(), "Visual graph strict native codegen did not return structured diagnostics");
    kb::tests::Require(strictPartialGenerated.diagnostics[0].stage == kb::visual::VisualGraphDiagnosticStage::NativeCodegen,
        "Visual graph strict native diagnostic did not preserve codegen stage");
    kb::tests::Require(strictPartialGenerated.diagnostics[0].nodeId != 0U, "Visual graph strict native diagnostic did not identify the failing node");

    kb::visual::VisualGraphNativeBindingRegistry strictBindings;
    kb::tests::Require(strictBindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetComponent,
                           .symbol = "TransformComponent",
                           .functionName = "NativeTransformComponent",
                       }),
        "Visual graph strict native component binding registration failed");
    kb::tests::Require(strictBindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "CacheTransform",
                           .functionName = "NativeCacheTransform",
                       }),
        "Visual graph strict native cache binding registration failed");
    kb::tests::Require(strictBindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "DeltaSeconds",
                           .functionName = "NativeDeltaSeconds",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                       }),
        "Visual graph strict native property binding registration failed");
    kb::tests::Require(strictBindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "MovePlayer",
                           .functionName = "NativeMovePlayer",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "delta", .type = kb::visual::VisualGraphValueType::Float},
                           },
                       }),
        "Visual graph strict native movement binding registration failed");
    const kb::visual::VisualGraphNativeCode strictGenerated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "PlayerController",
        .namespaceName = "kb::game",
        .bindings = &strictBindings,
        .requireNativeBindings = true,
    });
    kb::tests::Require(strictGenerated.Succeeded(), "Visual graph native codegen rejected strict native mode with complete bindings");

    const kb::visual::VisualGraphBuildResult built = kb::visual::VisualGraphBuildPipeline::BuildNative(*graph.Get(), kb::visual::VisualGraphBuildDesc{
        .nativeCodegen = kb::visual::VisualGraphNativeCodegenDesc{
            .className = "PlayerController",
            .namespaceName = "kb::game",
            .bindings = &bindings,
        },
    });
    kb::tests::Require(built.Succeeded(), "Visual graph native build pipeline rejected a valid graph");
    kb::tests::Require(built.nativeCode.source.find("NativeMovePlayer(context, context.ReadFloat(20U, \"value\"));") != std::string::npos,
        "Visual graph native build pipeline did not use direct typed bindings");

    kb::visual::VisualGraphNativeBindingRegistry mismatchedBindings;
    kb::tests::Require(mismatchedBindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "MovePlayer",
                           .functionName = "NativeMovePlayer",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{
                                   .name = "missing",
                                   .type = kb::visual::VisualGraphValueType::Float,
                               },
                           },
                       }),
        "Visual graph mismatched native binding registration failed");
    const kb::visual::VisualGraphNativeCode mismatchedGenerated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "PlayerController",
        .namespaceName = "kb::game",
        .bindings = &mismatchedBindings,
    });
    kb::tests::Require(!mismatchedGenerated.Succeeded(), "Visual graph native codegen accepted a binding with a missing required input");
    kb::tests::Require(mismatchedGenerated.source.find("context.CallNative(\"MovePlayer\")") == std::string::npos,
        "Visual graph native codegen fell back to dispatch for a mapped but invalid binding");

    kb::visual::VisualGraphNativeBindingRegistry multiOutputBindings;
    kb::tests::Require(multiOutputBindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "DeltaSeconds",
                           .functionName = "NativeDeltaSeconds",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                               kb::visual::VisualGraphNativePinSignature{.name = "extra", .type = kb::visual::VisualGraphValueType::Float, .required = false},
                           },
                       }),
        "Visual graph multi-output native binding registration failed");
    const kb::visual::VisualGraphNativeCode multiOutputGenerated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "PlayerController",
        .namespaceName = "kb::game",
        .bindings = &multiOutputBindings,
    });
    kb::tests::Require(!multiOutputGenerated.Succeeded(), "Visual graph native codegen accepted a multi-output function binding without an explicit statement");
}

void RunVisualGraphCompileServicePreparesRuntimeArtifactTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "Project";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    const std::filesystem::path generatedRoot = projectRoot / "Intermediate" / "GeneratedVisualScripts";
    WriteTextFile(assetsRoot / "Logic" / "PlayerController.kbgraph", SampleGraphText());

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>()), "Visual graph loader registration failed for compile service test");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Visual graph compile service asset mount failed");
    static_cast<void>(manager.DiscoverMountedAssets());

    const kb::assets::AssetHandle<kb::visual::VisualGraphAsset> graph = manager.Load<kb::visual::VisualGraphAsset>("/Game/Logic/PlayerController.kbgraph");
    kb::tests::Require(graph.IsLoaded(), "Visual graph compile service test asset did not load");

    kb::visual::VisualGraphNativeBindingRegistry bindings;
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "DeltaSeconds",
                           .functionName = "NativeDeltaSeconds",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                       }),
        "Visual graph compile service output binding registration failed");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "MovePlayer",
                           .functionName = "NativeMovePlayer",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "delta", .type = kb::visual::VisualGraphValueType::Float},
                           },
                       }),
        "Visual graph compile service input binding registration failed");

    kb::visual::VisualGraphRuntimeRegistry runtimeRegistry;
    const kb::visual::VisualGraphCompileServiceResult compiled = kb::visual::VisualGraphCompileService::CompileAsset(
        manager,
        graph.Id(),
        kb::visual::VisualGraphCompileRequest{
            .build = kb::visual::VisualGraphBuildDesc{
                .nativeCodegen = kb::visual::VisualGraphNativeCodegenDesc{
                    .className = "PlayerController",
                    .namespaceName = "kb::game",
                    .bindings = &bindings,
                },
            },
            .generatedCodeDirectory = generatedRoot,
            .writeGeneratedCode = true,
        },
        runtimeRegistry);

    kb::tests::Require(compiled.Succeeded(), "Visual graph compile service rejected a valid graph");
    kb::tests::Require(compiled.diagnostics.empty(), "Visual graph compile service produced diagnostics for a valid graph");
    kb::tests::Require(runtimeRegistry.Contains(graph.Id()), "Visual graph compile service did not register the runtime artifact");
    const kb::visual::VisualGraphRuntimeArtifact* artifact = runtimeRegistry.Find(graph.Id());
    kb::tests::Require(artifact != nullptr, "Visual graph runtime artifact was not queryable");
    kb::tests::Require(artifact->assetId == graph.Id(), "Visual graph runtime artifact stored the wrong asset id");
    kb::tests::Require(artifact->module.functions.size() == 2U, "Visual graph runtime artifact did not retain compiled IR");
    kb::tests::Require(std::filesystem::is_regular_file(artifact->generatedFiles.headerPath), "Visual graph compile service did not write the generated header");
    kb::tests::Require(std::filesystem::is_regular_file(artifact->generatedFiles.sourcePath), "Visual graph compile service did not write the generated source");
    kb::tests::Require(artifact->nativeCode.source.find("NativeMovePlayer(context, context.ReadFloat(20U, \"value\"));") != std::string::npos,
        "Visual graph runtime artifact did not retain direct typed native code");

    kb::scene::BehaviourComponent behaviour{
        .behaviourAssetId = graph.Id().value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    };
    kb::tests::Require(runtimeRegistry.Contains(kb::assets::AssetId{behaviour.behaviourAssetId}), "Visual graph behaviour asset id does not resolve to a compiled runtime artifact");

    kb::visual::VisualGraphRuntimeRegistry failingRuntimeRegistry;
    const kb::visual::VisualGraphCompileServiceResult failed = kb::visual::VisualGraphCompileService::CompileAsset(
        manager,
        kb::assets::AssetId{},
        kb::visual::VisualGraphCompileRequest{
            .writeGeneratedCode = false,
        },
        failingRuntimeRegistry);
    kb::tests::Require(!failed.Succeeded(), "Visual graph compile service accepted an invalid asset id");
    kb::tests::Require(!failed.diagnostics.empty(), "Visual graph compile service did not return structured diagnostics");
    kb::tests::Require(failed.diagnostics[0].severity == kb::visual::VisualGraphDiagnosticSeverity::Error,
        "Visual graph compile service diagnostic did not preserve severity");
    kb::tests::Require(failed.diagnostics[0].stage == kb::visual::VisualGraphDiagnosticStage::AssetLoad,
        "Visual graph compile service diagnostic did not preserve stage");
}

void RunVisualGraphCompileCoordinatorTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "CoordinatorProject";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    const std::filesystem::path generatedRoot = projectRoot / "Intermediate" / "GeneratedVisualScripts";
    const std::filesystem::path nativeBuildMarker = projectRoot / "Intermediate" / "native_build.marker";
    WriteTextFile(assetsRoot / "Logic" / "PlayerController.kbgraph", SampleGraphText());
    WriteTextFile(assetsRoot / "Logic" / "ApiNames.lua", R"(-- @expose speed Float = 1.0
function Tick(self, dt)
    local Send = Emit
    -- @apix event Collected.InvalidBoundary
    SetShared("Collected.Score", 7)
    Emit("Collected.Event")
    Send("Collected.AliasEvent")
    CallFunction("Collected.Function", { value = 1 })
end
)");

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>()), "Visual graph coordinator loader registration failed");
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::script::LuaScriptAssetLoader>()), "Visual graph coordinator Lua loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Visual graph coordinator asset mount failed");
    static_cast<void>(manager.DiscoverMountedAssets());

    const kb::script::ScriptApiNameCollectionResult collectedApiNames = kb::script::ScriptApiNameCollector::CollectProjectAssets(manager);
    kb::tests::Require(collectedApiNames.Succeeded(), "Visual graph coordinator API name collection produced diagnostics");
    kb::tests::Require(collectedApiNames.names.Contains(kb::script::ScriptApiNameKind::ExposedVariable, "speed"),
        "Lua API name collection did not register exposed variables");
    kb::tests::Require(collectedApiNames.names.Contains(kb::script::ScriptApiNameKind::Event, "Collected.AliasEvent"),
        "Lua API name collection did not register literal event calls through local aliases");
    kb::tests::Require(!collectedApiNames.names.Contains(kb::script::ScriptApiNameKind::Event, "Collected.InvalidBoundary"),
        "Lua API declaration parser accepted @apix as @api");

    const kb::assets::AssetHandle<kb::visual::VisualGraphAsset> graph = manager.Load<kb::visual::VisualGraphAsset>("/Game/Logic/PlayerController.kbgraph");
    kb::tests::Require(graph.IsLoaded(), "Visual graph coordinator test asset did not load");

    kb::visual::VisualGraphNativeBindingRegistry bindings;
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "DeltaSeconds",
                           .functionName = "NativeDeltaSeconds",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Float },
                           },
                       }),
        "Visual graph coordinator output binding registration failed");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "MovePlayer",
                           .functionName = "NativeMovePlayer",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{ .name = "delta", .type = kb::visual::VisualGraphValueType::Float },
                           },
                       }),
        "Visual graph coordinator input binding registration failed");

    kb::script::ScriptApiNameRegistry apiNames;
    kb::tests::Require(apiNames.Register(kb::script::ScriptApiNameKind::Function, "Inventory.AddItem", "InventoryService"), "Script API name registry did not accept function name");
    kb::visual::VisualGraphRuntimeRegistry runtimeRegistry;
    const kb::visual::VisualGraphCompileCoordinatorResult compiled = kb::visual::VisualGraphCompileCoordinator::Compile(
        manager,
        kb::visual::VisualGraphCompileCoordinatorRequest{
            .assetId = graph.Id(),
            .build = kb::visual::VisualGraphBuildDesc{
                .nativeCodegen = kb::visual::VisualGraphNativeCodegenDesc{
                    .className = "PlayerController",
                    .namespaceName = "kb::game",
                    .bindings = &bindings,
                },
            },
            .generatedCodeDirectory = generatedRoot,
            .apiNames = &apiNames,
            .writeGeneratedCode = false,
            .nativeBuild = kb::visual::VisualGraphNativeBuildDesc{
                .enabled = true,
                .command = std::string{ "cmake -E touch \"" } + nativeBuildMarker.string() + "\"",
                .workingDirectory = projectRoot,
            },
            .storeRuntimeArtifact = true,
        },
        runtimeRegistry);
    kb::tests::Require(compiled.Succeeded(), "Visual graph compile coordinator rejected a valid request");
    kb::tests::Require(compiled.runtimeArtifactStored, "Visual graph compile coordinator did not store the runtime artifact");
    kb::tests::Require(compiled.nativeBuildSucceeded, "Visual graph compile coordinator did not report native build success");
    kb::tests::Require(runtimeRegistry.Contains(graph.Id()), "Visual graph compile coordinator runtime registry is missing the artifact");
    const kb::visual::VisualGraphRuntimeArtifact* artifact = runtimeRegistry.Find(graph.Id());
    kb::tests::Require(artifact != nullptr, "Visual graph compile coordinator artifact was not queryable");
    kb::tests::Require(std::filesystem::is_regular_file(artifact->generatedFiles.headerPath), "Visual graph compile coordinator did not write generated header");
    kb::tests::Require(std::filesystem::is_regular_file(artifact->generatedFiles.sourcePath), "Visual graph compile coordinator did not write generated source");
    kb::tests::Require(std::filesystem::is_regular_file(nativeBuildMarker), "Visual graph compile coordinator did not execute native build command");

    kb::script::ScriptApiNameRegistry crossKindApiNames;
    kb::tests::Require(crossKindApiNames.Register(kb::script::ScriptApiNameKind::Function, "Collected.Score", "FunctionApi"), "Script API cross-kind test name did not register");
    kb::visual::VisualGraphRuntimeRegistry crossKindFailingRegistry;
    const kb::visual::VisualGraphCompileCoordinatorResult crossKindFailed = kb::visual::VisualGraphCompileCoordinator::Compile(
        manager,
        kb::visual::VisualGraphCompileCoordinatorRequest{
            .assetId = graph.Id(),
            .apiNames = &crossKindApiNames,
            .disallowCrossKindApiNameCollisions = true,
            .writeGeneratedCode = false,
            .storeRuntimeArtifact = true,
        },
        crossKindFailingRegistry);
    kb::tests::Require(!crossKindFailed.Succeeded(), "Visual graph compile coordinator accepted collected cross-kind API name collision");
    kb::tests::Require(!crossKindFailingRegistry.Contains(graph.Id()), "Visual graph compile coordinator stored artifact after collected API name collision");

    WriteTextFile(assetsRoot / "Logic" / "ApiNamesConflict.lua", R"(
function Tick(self, dt)
    SetShared("Collected.Score", "invalid")
end
)");
    static_cast<void>(manager.DiscoverMountedAssets());
    kb::visual::VisualGraphRuntimeRegistry sharedContractFailingRegistry;
    const kb::visual::VisualGraphCompileCoordinatorResult sharedContractFailed = kb::visual::VisualGraphCompileCoordinator::Compile(
        manager,
        kb::visual::VisualGraphCompileCoordinatorRequest{
            .assetId = graph.Id(),
            .writeGeneratedCode = false,
            .storeRuntimeArtifact = true,
        },
        sharedContractFailingRegistry);
    kb::tests::Require(!sharedContractFailed.Succeeded(), "Visual graph compile coordinator accepted conflicting collected shared key contracts");
    kb::tests::Require(!sharedContractFailed.diagnostics.empty() && sharedContractFailed.diagnostics[0].stage == kb::visual::VisualGraphDiagnosticStage::ApiNameValidation,
        "Visual graph compile coordinator did not report shared key API contract diagnostics");
    kb::tests::Require(!sharedContractFailingRegistry.Contains(graph.Id()), "Visual graph compile coordinator stored artifact after shared key API contract failure");

    ResetTestRoot();
    const std::filesystem::path signatureProjectRoot = TestRoot() / "SignatureProject";
    const std::filesystem::path signatureAssetsRoot = signatureProjectRoot / "Assets";
    WriteTextFile(signatureAssetsRoot / "Logic" / "PlayerController.kbgraph", SampleGraphText());
    WriteTextFile(signatureAssetsRoot / "Logic" / "InventoryCaller.kbgraph", FunctionSignatureGraphText());
    kb::assets::AssetManager signatureManager;
    kb::tests::Require(signatureManager.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>()), "Visual graph signature loader registration failed");
    kb::tests::Require(signatureManager.Mounts().Mount("Game", signatureAssetsRoot), "Visual graph signature asset mount failed");
    static_cast<void>(signatureManager.DiscoverMountedAssets());
    const kb::assets::AssetHandle<kb::visual::VisualGraphAsset> signatureGraph = signatureManager.Load<kb::visual::VisualGraphAsset>("/Game/Logic/PlayerController.kbgraph");
    kb::tests::Require(signatureGraph.IsLoaded(), "Visual graph signature test asset did not load");
    kb::script::ScriptApiNameRegistry signatureApiNames;
    const std::vector<kb::script::ScriptApiPin> addItemInputs{
        kb::script::ScriptApiPin{ .name = "itemId", .type = kb::script::ScriptValueType::Int },
    };
    const std::vector<kb::script::ScriptApiPin> addItemOutputs{
        kb::script::ScriptApiPin{ .name = "total", .type = kb::script::ScriptValueType::Int, .required = false },
    };
    kb::tests::Require(signatureApiNames.RegisterFunction("Inventory.AddItem", addItemInputs, addItemOutputs, "InventoryService", true),
        "Script API signature test function did not register");
    kb::visual::VisualGraphRuntimeRegistry functionContractFailingRegistry;
    const kb::visual::VisualGraphCompileCoordinatorResult functionContractFailed = kb::visual::VisualGraphCompileCoordinator::Compile(
        signatureManager,
        kb::visual::VisualGraphCompileCoordinatorRequest{
            .assetId = signatureGraph.Id(),
            .apiNames = &signatureApiNames,
            .writeGeneratedCode = false,
            .storeRuntimeArtifact = true,
        },
        functionContractFailingRegistry);
    kb::tests::Require(!functionContractFailed.Succeeded(), "Visual graph compile coordinator accepted conflicting function API contracts");
    kb::tests::Require(!functionContractFailingRegistry.Contains(signatureGraph.Id()), "Visual graph compile coordinator stored artifact after function API contract failure");

    kb::script::ScriptApiNameRegistry duplicateApiNames;
    kb::tests::Require(duplicateApiNames.RegisterFunction("Inventory.AddItem", addItemInputs, addItemOutputs, "InventoryServiceA", true),
        "Script API duplicate provider test first name did not register");
    kb::tests::Require(duplicateApiNames.RegisterFunction("Inventory.AddItem", addItemInputs, addItemOutputs, "InventoryServiceB", true),
        "Script API duplicate provider test second name did not register");
    kb::visual::VisualGraphRuntimeRegistry failingRegistry;
    const kb::visual::VisualGraphCompileCoordinatorResult failed = kb::visual::VisualGraphCompileCoordinator::Compile(
        signatureManager,
        kb::visual::VisualGraphCompileCoordinatorRequest{
            .assetId = signatureGraph.Id(),
            .apiNames = &duplicateApiNames,
            .collectProjectApiNames = false,
            .writeGeneratedCode = false,
            .storeRuntimeArtifact = true,
        },
        failingRegistry);
    kb::tests::Require(!failed.Succeeded(), "Visual graph compile coordinator accepted duplicate function providers");
    kb::tests::Require(!failed.diagnostics.empty() && failed.diagnostics[0].stage == kb::visual::VisualGraphDiagnosticStage::ApiNameValidation,
        "Visual graph compile coordinator did not report API name validation diagnostics");
    kb::tests::Require(!failingRegistry.Contains(signatureGraph.Id()), "Visual graph compile coordinator stored runtime artifact after API validation failure");
}

void RunVisualGraphRuntimeExecutorTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "Project";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "PlayerController.kbgraph", SampleGraphText());

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>()), "Visual graph loader registration failed for runtime executor test");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Visual graph runtime executor asset mount failed");
    static_cast<void>(manager.DiscoverMountedAssets());

    const kb::assets::AssetHandle<kb::visual::VisualGraphAsset> graph = manager.Load<kb::visual::VisualGraphAsset>("/Game/Logic/PlayerController.kbgraph");
    kb::tests::Require(graph.IsLoaded(), "Visual graph runtime executor test asset did not load");

    kb::visual::VisualGraphRuntimeRegistry runtimeRegistry;
    const kb::visual::VisualGraphCompileServiceResult compiled = kb::visual::VisualGraphCompileService::CompileAsset(
        manager,
        graph.Id(),
        kb::visual::VisualGraphCompileRequest{
            .build = kb::visual::VisualGraphBuildDesc{
                .nativeCodegen = kb::visual::VisualGraphNativeCodegenDesc{
                    .className = "PlayerController",
                    .namespaceName = "kb::game",
                },
            },
            .writeGeneratedCode = false,
        },
        runtimeRegistry);
    kb::tests::Require(compiled.Succeeded(), "Visual graph runtime executor compile step failed");
    const kb::visual::VisualGraphRuntimeArtifact* artifact = runtimeRegistry.Find(graph.Id());
    kb::tests::Require(artifact != nullptr, "Visual graph runtime executor did not get a runtime artifact");

    float movedBy = 0.0F;
    kb::visual::VisualGraphRuntimeBindingRegistry runtimeBindings;
    kb::tests::Require(runtimeBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "DeltaSeconds",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{0.25F});
                           },
                       }),
        "Visual graph runtime output binding registration failed");
    kb::tests::Require(runtimeBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "MovePlayer",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "delta", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [&movedBy](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               const kb::visual::VisualGraphIrInput& delta = instruction.inputs[0];
                               movedBy += context.ReadFloat(delta.sourceNodeId, delta.sourcePin);
                           },
                       }),
        "Visual graph runtime input binding registration failed");

    kb::visual::VisualGraphRuntimeExecutionContext context;
    const kb::visual::VisualGraphRuntimeExecutor executor{runtimeBindings};
    const kb::visual::VisualGraphRuntimeExecutionResult executed = executor.Execute(*artifact, kb::visual::VisualGraphLifecycleEvent::Tick, context);
    kb::tests::Require(executed.Succeeded(), "Visual graph runtime executor rejected a compiled Tick graph");
    kb::tests::Require(movedBy == 0.25F, "Visual graph runtime executor did not pass typed data input to native callback");
    kb::tests::Require(context.EmittedEvents().size() == 1U && context.EmittedEvents()[0] == "PlayerMoved", "Visual graph runtime executor did not emit graph events");
    kb::tests::Require(context.EmittedEventRecords().size() == 1U && context.EmittedEventRecords()[0].arguments.empty(),
        "Visual graph runtime executor did not expose graph event records");
    kb::visual::VisualGraphRuntimeExecutionContext emptyEventContext;
    emptyEventContext.EmitEvent("");
    kb::tests::Require(emptyEventContext.EmittedEvents().empty() && emptyEventContext.EmittedEventRecords().empty(), "Visual graph runtime context accepted an empty event name");

    const kb::visual::VisualGraphRuntimeExecutionResult readyExecuted = executor.Execute(*artifact, kb::visual::VisualGraphLifecycleEvent::Ready, context);
    kb::tests::Require(!readyExecuted.Succeeded(), "Visual graph runtime executor accepted Ready without required native bindings");

    kb::visual::VisualGraphRuntimeExecutionContext behaviourContext;
    const kb::visual::VisualGraphBehaviourExecutionResult behaviourExecuted = kb::visual::VisualGraphBehaviourRuntime::Execute(
        kb::scene::BehaviourComponent{
            .behaviourAssetId = graph.Id().value,
            .backend = kb::scene::BehaviourBackend::VisualGraph,
            .enabled = true,
        },
        kb::scene::SceneEntity{77U},
        kb::visual::VisualGraphLifecycleEvent::Tick,
        runtimeRegistry,
        runtimeBindings,
        behaviourContext);
    kb::tests::Require(behaviourExecuted.Succeeded(), "Visual graph behaviour runtime rejected a valid visual graph behaviour");
    kb::tests::Require(behaviourContext.ReadUInt64(0U, "self") == 77U, "Visual graph behaviour runtime did not expose the owner entity");

    bool missingOutputConsumerRan = false;
    kb::visual::VisualGraphRuntimeBindingRegistry missingOutputBindings;
    kb::tests::Require(missingOutputBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "DeltaSeconds",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext&, const kb::visual::VisualGraphIrInstruction&) {},
                       }),
        "Visual graph runtime missing-output producer binding registration failed");
    kb::tests::Require(missingOutputBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "MovePlayer",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "delta", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [&missingOutputConsumerRan](kb::visual::VisualGraphRuntimeExecutionContext&, const kb::visual::VisualGraphIrInstruction&) {
                               missingOutputConsumerRan = true;
                           },
                       }),
        "Visual graph runtime missing-output consumer binding registration failed");
    kb::visual::VisualGraphRuntimeExecutionContext missingOutputContext;
    missingOutputContext.Store(20U, "value", kb::visual::VisualGraphRuntimeValue{1.0F});
    const kb::visual::VisualGraphRuntimeExecutor missingOutputExecutor{missingOutputBindings};
    const kb::visual::VisualGraphRuntimeExecutionResult missingOutputExecuted = missingOutputExecutor.Execute(*artifact, kb::visual::VisualGraphLifecycleEvent::Tick, missingOutputContext);
    kb::tests::Require(!missingOutputExecuted.Succeeded(), "Visual graph runtime executor accepted a missing required binding output using a stale context value");
    kb::tests::Require(!missingOutputConsumerRan, "Visual graph runtime executor continued after a missing required binding output using a stale context value");
    kb::tests::Require(!missingOutputExecuted.diagnostics.empty(), "Visual graph runtime executor did not return structured diagnostics for missing output");
    kb::tests::Require(missingOutputExecuted.diagnostics[0].stage == kb::visual::VisualGraphDiagnosticStage::Runtime,
        "Visual graph runtime diagnostic did not preserve runtime stage");
    kb::tests::Require(missingOutputExecuted.diagnostics[0].nodeId == 20U, "Visual graph runtime diagnostic did not identify the producer node");
    kb::tests::Require(missingOutputExecuted.diagnostics[0].pinName == "value", "Visual graph runtime diagnostic did not identify the missing output pin");

    bool mismatchedCallbackRan = false;
    kb::visual::VisualGraphRuntimeBindingRegistry mismatchedBindings;
    kb::tests::Require(mismatchedBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "DeltaSeconds",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& runtimeContext, const kb::visual::VisualGraphIrInstruction& instruction) {
                               runtimeContext.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{0.5F});
                           },
                       }),
        "Visual graph runtime mismatched output binding registration failed");
    kb::tests::Require(mismatchedBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "MovePlayer",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "delta", .type = kb::visual::VisualGraphValueType::Int},
                           },
                           .callback = [&mismatchedCallbackRan](kb::visual::VisualGraphRuntimeExecutionContext&, const kb::visual::VisualGraphIrInstruction&) {
                               mismatchedCallbackRan = true;
                           },
                       }),
        "Visual graph runtime mismatched input binding registration failed");

    kb::visual::VisualGraphRuntimeExecutionContext mismatchedContext;
    const kb::visual::VisualGraphRuntimeExecutor mismatchedExecutor{mismatchedBindings};
    const kb::visual::VisualGraphRuntimeExecutionResult mismatchedExecuted = mismatchedExecutor.Execute(*artifact, kb::visual::VisualGraphLifecycleEvent::Tick, mismatchedContext);
    kb::tests::Require(!mismatchedExecuted.Succeeded(), "Visual graph runtime executor accepted a mismatched binding signature");
    kb::tests::Require(!mismatchedCallbackRan, "Visual graph runtime executor ran a callback with a mismatched binding signature");
    kb::tests::Require(mismatchedContext.EmittedEvents().empty(), "Visual graph runtime executor continued execution after a binding signature error");
}

void RunVisualGraphBehaviourLifecycleRunnerTest() {
    ResetTestRoot();
    const std::filesystem::path projectRoot = TestRoot() / "Project";
    const std::filesystem::path assetsRoot = projectRoot / "Assets";
    WriteTextFile(assetsRoot / "Logic" / "PlayerController.kbgraph", SampleGraphText());

    kb::assets::AssetManager manager;
    kb::tests::Require(manager.RegisterLoader(std::make_unique<kb::visual::VisualGraphAssetLoader>()), "Visual graph lifecycle runner loader registration failed");
    kb::tests::Require(manager.Mounts().Mount("Game", assetsRoot), "Visual graph lifecycle runner asset mount failed");
    static_cast<void>(manager.DiscoverMountedAssets());

    const kb::assets::AssetHandle<kb::visual::VisualGraphAsset> graph = manager.Load<kb::visual::VisualGraphAsset>("/Game/Logic/PlayerController.kbgraph");
    kb::tests::Require(graph.IsLoaded(), "Visual graph lifecycle runner graph did not load");

    kb::visual::VisualGraphRuntimeRegistry runtimeRegistry;
    const kb::visual::VisualGraphCompileServiceResult compiled = kb::visual::VisualGraphCompileService::CompileAsset(
        manager,
        graph.Id(),
        kb::visual::VisualGraphCompileRequest{
            .build = kb::visual::VisualGraphBuildDesc{
                .nativeCodegen = kb::visual::VisualGraphNativeCodegenDesc{
                    .className = "PlayerController",
                    .namespaceName = "kb::game",
                },
            },
            .writeGeneratedCode = false,
        },
        runtimeRegistry);
    kb::tests::Require(compiled.Succeeded(), "Visual graph lifecycle runner compile step failed");

    kb::scene::Scene scene;
    const kb::scene::SceneObject player = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Player"});
    const kb::scene::SceneObject enemy = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Enemy"});
    const kb::scene::SceneObject luaObject = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{.name = "Lua"});

    scene.Components().Behaviours().Set(player.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = graph.Id().value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    });
    scene.Components().Behaviours().Set(enemy.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = graph.Id().value,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    });
    scene.Components().Behaviours().Set(luaObject.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = graph.Id().value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    std::vector<std::uint64_t> movedEntities;
    kb::visual::VisualGraphRuntimeBindingRegistry runtimeBindings;
    kb::tests::Require(runtimeBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "DeltaSeconds",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{0.016F});
                           },
                       }),
        "Visual graph lifecycle runner output binding registration failed");
    kb::tests::Require(runtimeBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "MovePlayer",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "delta", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [&movedEntities](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction&) {
                               movedEntities.push_back(context.ReadUInt64(0U, "self"));
                           },
                       }),
        "Visual graph lifecycle runner input binding registration failed");

    kb::visual::VisualGraphBehaviourInstanceRegistry instances;
    const kb::visual::VisualGraphBehaviourLifecycleResult firstTick = kb::visual::VisualGraphBehaviourLifecycleRunner::Execute(
        scene,
        kb::visual::VisualGraphLifecycleEvent::Tick,
        runtimeRegistry,
        runtimeBindings,
        instances);

    kb::tests::Require(firstTick.Succeeded(), "Visual graph lifecycle runner rejected valid scene behaviours");
    kb::tests::Require(firstTick.visitedBehaviours == 3U, "Visual graph lifecycle runner did not visit all BehaviourComponent entries");
    kb::tests::Require(firstTick.executedBehaviours == 2U, "Visual graph lifecycle runner did not execute only visual graph behaviours");
    kb::tests::Require(movedEntities.size() == 2U, "Visual graph lifecycle runner did not execute both visual graph instances");
    kb::tests::Require(firstTick.emittedEvents.size() == 2U, "Visual graph lifecycle runner did not expose emitted graph events");
    kb::tests::Require(firstTick.emittedEvents[0].name == "PlayerMoved", "Visual graph lifecycle runner emitted the wrong event name");
    kb::tests::Require(firstTick.emittedEvents[0].assetId == graph.Id(), "Visual graph lifecycle runner emitted an event with the wrong asset id");
    kb::tests::Require(instances.Count() == 2U, "Visual graph lifecycle runner did not create per-entity visual graph instances");
    kb::tests::Require(instances.Find(player.Entity(), graph.Id()) != nullptr, "Visual graph lifecycle runner did not retain player graph instance");
    kb::tests::Require(instances.Find(enemy.Entity(), graph.Id()) != nullptr, "Visual graph lifecycle runner did not retain enemy graph instance");

    scene.Components().Behaviours().TryGet(enemy.Entity())->enabled = false;
    scene.Components().Behaviours().MarkModified(enemy.Entity());
    movedEntities.clear();

    const kb::visual::VisualGraphBehaviourLifecycleResult secondTick = kb::visual::VisualGraphBehaviourLifecycleRunner::Execute(
        scene,
        kb::visual::VisualGraphLifecycleEvent::Tick,
        runtimeRegistry,
        runtimeBindings,
        instances);

    kb::tests::Require(secondTick.Succeeded(), "Visual graph lifecycle runner rejected a scene with a disabled behaviour");
    kb::tests::Require(secondTick.visitedBehaviours == 3U, "Visual graph lifecycle runner skipped component iteration after disabling a behaviour");
    kb::tests::Require(secondTick.executedBehaviours == 1U, "Visual graph lifecycle runner executed a disabled visual graph behaviour");
    kb::tests::Require(movedEntities.size() == 1U && movedEntities[0] == player.Entity().Id(), "Visual graph lifecycle runner did not preserve owner entity context");
    kb::tests::Require(secondTick.emittedEvents.size() == 1U && secondTick.emittedEvents[0].sender == player.Entity(), "Visual graph lifecycle runner emitted an event for the wrong sender");
    kb::tests::Require(instances.Count() == 2U, "Visual graph lifecycle runner discarded existing instance state unexpectedly");
}

void RunVisualGraphCompilerRejectsInvalidEdgesTest() {
    kb::visual::VisualGraphAsset graph{};
    graph.name = "Broken";
    graph.nodes.push_back(kb::visual::VisualGraphNode{
        .id = 1,
        .kind = kb::visual::VisualGraphNodeKind::Event,
        .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick,
    });
    graph.edges.push_back(kb::visual::VisualGraphEdge{
        .fromNode = 1,
        .toNode = 99,
    });

    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(!compiled.Succeeded(), "Visual graph compiler accepted an edge to a missing node");
}

void RunVisualGraphCompilerRejectsInvalidDataPinsTest() {
    kb::visual::VisualGraphAsset graph{};
    graph.name = "InvalidDataPins";
    graph.nodes = {
        kb::visual::VisualGraphNode{.id = 1, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick},
        kb::visual::VisualGraphNode{.id = 2, .kind = kb::visual::VisualGraphNodeKind::Sequence},
        kb::visual::VisualGraphNode{.id = 3, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "Consume"},
    };
    graph.pins = {
        kb::visual::VisualGraphPin{.nodeId = 2, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 3, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "value", .type = kb::visual::VisualGraphValueType::Void},
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{.fromNode = 2, .fromPin = "then", .toNode = 3, .toPin = "value", .kind = kb::visual::VisualGraphEdgeKind::Data},
    };

    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(!compiled.Succeeded(), "Visual graph compiler accepted a data edge using Void pins");
    kb::tests::Require(!compiled.diagnostics.empty(), "Visual graph compiler did not return diagnostics for invalid data pins");
    kb::tests::Require(compiled.diagnostics[0].stage == kb::visual::VisualGraphDiagnosticStage::Validation,
        "Visual graph invalid data pin diagnostic did not preserve validation stage");
    kb::tests::Require(compiled.diagnostics[0].nodeId == 3U, "Visual graph invalid data pin diagnostic did not identify the target node");
    kb::tests::Require(compiled.diagnostics[0].pinName == "value", "Visual graph invalid data pin diagnostic did not identify the target pin");
}

void RunVisualGraphCompilerRejectsCyclesTest() {
    kb::visual::VisualGraphAsset executionGraph{};
    executionGraph.name = "ExecutionCycle";
    executionGraph.nodes = {
        kb::visual::VisualGraphNode{.id = 1, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick},
        kb::visual::VisualGraphNode{.id = 2, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "A"},
        kb::visual::VisualGraphNode{.id = 3, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "B"},
    };
    executionGraph.pins = {
        kb::visual::VisualGraphPin{.nodeId = 1, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 2, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 2, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 3, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 3, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
    };
    executionGraph.edges = {
        kb::visual::VisualGraphEdge{.fromNode = 1, .fromPin = "then", .toNode = 2, .toPin = "exec"},
        kb::visual::VisualGraphEdge{.fromNode = 2, .fromPin = "then", .toNode = 3, .toPin = "exec"},
        kb::visual::VisualGraphEdge{.fromNode = 3, .fromPin = "then", .toNode = 2, .toPin = "exec"},
    };

    const kb::visual::VisualGraphCompileResult executionCompiled = kb::visual::VisualGraphCompiler::Compile(executionGraph);
    kb::tests::Require(!executionCompiled.Succeeded(), "Visual graph compiler accepted an execution cycle");
    kb::tests::Require(!executionCompiled.diagnostics.empty() && executionCompiled.diagnostics[0].nodeId != 0U,
        "Visual graph compiler did not attach a node id to execution cycle diagnostics");

    kb::visual::VisualGraphAsset dataGraph{};
    dataGraph.name = "DataCycle";
    dataGraph.nodes = {
        kb::visual::VisualGraphNode{.id = 1, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick},
        kb::visual::VisualGraphNode{.id = 2, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "A"},
        kb::visual::VisualGraphNode{.id = 3, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "B"},
    };
    dataGraph.pins = {
        kb::visual::VisualGraphPin{.nodeId = 1, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 2, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "b", .type = kb::visual::VisualGraphValueType::Float},
        kb::visual::VisualGraphPin{.nodeId = 2, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "a", .type = kb::visual::VisualGraphValueType::Float},
        kb::visual::VisualGraphPin{.nodeId = 3, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "a", .type = kb::visual::VisualGraphValueType::Float},
        kb::visual::VisualGraphPin{.nodeId = 3, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "b", .type = kb::visual::VisualGraphValueType::Float},
    };
    dataGraph.edges = {
        kb::visual::VisualGraphEdge{.fromNode = 2, .fromPin = "a", .toNode = 3, .toPin = "a", .kind = kb::visual::VisualGraphEdgeKind::Data},
        kb::visual::VisualGraphEdge{.fromNode = 3, .fromPin = "b", .toNode = 2, .toPin = "b", .kind = kb::visual::VisualGraphEdgeKind::Data},
    };

    const kb::visual::VisualGraphCompileResult dataCompiled = kb::visual::VisualGraphCompiler::Compile(dataGraph);
    kb::tests::Require(!dataCompiled.Succeeded(), "Visual graph compiler accepted a data dependency cycle");
}

// LIB-101: watchdog for a control-flow loop the AUTHORING PIPELINE cannot
// see. RunVisualGraphCompilerRejectsCyclesTest above already proves
// VisualGraphValidator::ValidateAcyclicEdges rejects an execution-edge
// cycle authored through the normal VisualGraphAsset editor, BEFORE
// compilation ever produces IR — so a graph built and compiled the normal
// way can never contain a cyclic nextNodeId chain. This test instead
// constructs VisualGraphIrInstruction/VisualGraphIrFunction DIRECTLY,
// bypassing VisualGraphValidator/VisualGraphCompiler entirely — the one
// path (hand-authored or otherwise non-compiler-produced IR) the asset-level
// validator cannot protect — to prove VisualGraphRuntimeExecutor::ExecuteNode
// itself has an independent, second-layer guard (kMaxVisualGraphExecutionSteps)
// against an unbounded forward-flow loop, rather than relying solely on the
// validator upstream.
void RunVisualGraphRuntimeControlFlowLoopWatchdogTest() {
    kb::visual::VisualGraphIrFunction function{};
    function.event = kb::visual::VisualGraphLifecycleEvent::Tick;
    function.entryNodeId = 1U;
    function.instructions = {
        kb::visual::VisualGraphIrInstruction{ .opcode = kb::visual::VisualGraphIrOpcode::Sequence, .sourceNodeId = 1U, .nextNodeId = 2U },
        kb::visual::VisualGraphIrInstruction{ .opcode = kb::visual::VisualGraphIrOpcode::Sequence, .sourceNodeId = 2U, .nextNodeId = 1U },
    };

    kb::visual::VisualGraphRuntimeArtifact artifact{};
    artifact.module.functions = { function };

    const kb::visual::VisualGraphRuntimeBindingRegistry bindings;
    const kb::visual::VisualGraphRuntimeExecutor executor{ bindings };
    kb::visual::VisualGraphRuntimeExecutionContext context;
    const kb::visual::VisualGraphRuntimeExecutionResult result = executor.Execute(artifact, kb::visual::VisualGraphLifecycleEvent::Tick, context);

    kb::tests::Require(!result.Succeeded(), "A hand-authored control-flow loop (two Sequence nodes wired back to each other, bypassing the asset-level validator) must be caught as a runtime error, not hang or crash the engine");
    kb::tests::Require(!result.diagnostics.empty(), "The control-flow-loop watchdog must report a structured diagnostic, not just a bare error string");
    bool foundStepBudgetMessage = false;
    for (const std::string& error : result.errors) {
        if (error.find("step budget") != std::string::npos) {
            foundStepBudgetMessage = true;
            break;
        }
    }
    kb::tests::Require(foundStepBudgetMessage, "The watchdog's error message must clearly name the step-budget guard, not report a generic/unrelated failure");
}

void RunVisualGraphCompilerBranchTargetsTest() {
    kb::visual::VisualGraphAsset graph{};
    graph.name = "BranchGraph";
    graph.nodes = {
        kb::visual::VisualGraphNode{.id = 1, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick},
        kb::visual::VisualGraphNode{.id = 2, .kind = kb::visual::VisualGraphNodeKind::Branch},
        kb::visual::VisualGraphNode{.id = 3, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "OnTrue"},
        kb::visual::VisualGraphNode{.id = 4, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "OnFalse"},
    };
    graph.pins = {
        kb::visual::VisualGraphPin{.nodeId = 1, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 2, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 2, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "true", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 2, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "false", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 3, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 4, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{.fromNode = 1, .fromPin = "then", .toNode = 2, .toPin = "exec"},
        kb::visual::VisualGraphEdge{.fromNode = 2, .fromPin = "true", .toNode = 3, .toPin = "exec"},
        kb::visual::VisualGraphEdge{.fromNode = 2, .fromPin = "false", .toNode = 4, .toPin = "exec"},
    };

    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Visual graph compiler rejected a valid branch graph");
    kb::tests::Require(compiled.module.functions.size() == 1U, "Visual graph compiler did not produce a branch function");
    kb::tests::Require(compiled.module.functions[0].instructions.size() == 3U, "Visual graph compiler did not compile branch targets");
    kb::tests::Require(compiled.module.functions[0].instructions[0].trueNodeId == 3U, "Visual graph compiler did not preserve true branch target");
    kb::tests::Require(compiled.module.functions[0].instructions[0].falseNodeId == 4U, "Visual graph compiler did not preserve false branch target");

    const kb::visual::VisualGraphNativeCode generated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "BranchGraph",
        .namespaceName = "kb::game",
    });
    kb::tests::Require(generated.Succeeded(), "Visual graph native codegen rejected a valid branch graph");
    kb::tests::Require(generated.source.find("if (false)") != std::string::npos, "Visual graph native codegen did not emit branch control flow");
    kb::tests::Require(generated.source.find("BranchGraph::Execute_Tick_3(context);") != std::string::npos, "Visual graph native codegen did not emit true branch target");
    kb::tests::Require(generated.source.find("BranchGraph::Execute_Tick_4(context);") != std::string::npos, "Visual graph native codegen did not emit false branch target");
}

void RunVisualGraphRuntimeDataDependencyDoesNotFollowExecutionTest() {
    kb::visual::VisualGraphAsset graph{};
    graph.name = "DataDependencyGraph";
    graph.nodes = {
        kb::visual::VisualGraphNode{.id = 1, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick},
        kb::visual::VisualGraphNode{.id = 10, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "Value"},
        kb::visual::VisualGraphNode{.id = 11, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "UseValue"},
        kb::visual::VisualGraphNode{.id = 12, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "ShouldNotRun"},
    };
    graph.pins = {
        kb::visual::VisualGraphPin{.nodeId = 1, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 10, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::Float},
        kb::visual::VisualGraphPin{.nodeId = 10, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 11, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 11, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "value", .type = kb::visual::VisualGraphValueType::Float},
        kb::visual::VisualGraphPin{.nodeId = 12, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{.fromNode = 1, .fromPin = "then", .toNode = 11, .toPin = "exec"},
        kb::visual::VisualGraphEdge{.fromNode = 10, .fromPin = "value", .toNode = 11, .toPin = "value", .kind = kb::visual::VisualGraphEdgeKind::Data},
        kb::visual::VisualGraphEdge{.fromNode = 10, .fromPin = "then", .toNode = 12, .toPin = "exec"},
    };

    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Visual graph compiler rejected the data dependency runtime regression graph");
    kb::visual::VisualGraphRuntimeArtifact artifact{
        .assetId = kb::assets::AssetId{1U},
        .graphName = graph.name,
        .module = compiled.module,
    };

    bool sideEffectRan = false;
    float consumed = 0.0F;
    kb::visual::VisualGraphRuntimeBindingRegistry bindings;
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "Value",
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{2.0F});
                           },
                       }),
        "Visual graph runtime data dependency binding registration failed");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "UseValue",
                           .callback = [&consumed](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               consumed = context.ReadFloat(instruction.inputs[0].sourceNodeId, instruction.inputs[0].sourcePin);
                           },
                       }),
        "Visual graph runtime consumer binding registration failed");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "ShouldNotRun",
                           .callback = [&sideEffectRan](kb::visual::VisualGraphRuntimeExecutionContext&, const kb::visual::VisualGraphIrInstruction&) {
                               sideEffectRan = true;
                           },
                       }),
        "Visual graph runtime side effect binding registration failed");

    kb::visual::VisualGraphRuntimeExecutionContext context;
    const kb::visual::VisualGraphRuntimeExecutor executor{bindings};
    const kb::visual::VisualGraphRuntimeExecutionResult executed = executor.Execute(artifact, kb::visual::VisualGraphLifecycleEvent::Tick, context);
    kb::tests::Require(executed.Succeeded(), "Visual graph runtime executor rejected the data dependency regression graph");
    kb::tests::Require(consumed == 2.0F, "Visual graph runtime executor did not evaluate the data dependency");
    kb::tests::Require(!sideEffectRan, "Visual graph runtime executor followed execution flow while evaluating a data dependency");
}

void RunVisualGraphRuntimeDoesNotReevaluateExecutedDataProducerTest() {
    kb::visual::VisualGraphAsset graph{};
    graph.name = "ExecutedDataProducerGraph";
    graph.nodes = {
        kb::visual::VisualGraphNode{.id = 1, .kind = kb::visual::VisualGraphNodeKind::Event, .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick},
        kb::visual::VisualGraphNode{.id = 10, .kind = kb::visual::VisualGraphNodeKind::GetProperty, .symbol = "Value"},
        kb::visual::VisualGraphNode{.id = 11, .kind = kb::visual::VisualGraphNodeKind::CallNative, .symbol = "UseValue"},
    };
    graph.pins = {
        kb::visual::VisualGraphPin{.nodeId = 1, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 10, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 10, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "then", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 10, .direction = kb::visual::VisualGraphPinDirection::Output, .name = "value", .type = kb::visual::VisualGraphValueType::Float},
        kb::visual::VisualGraphPin{.nodeId = 11, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "exec", .type = kb::visual::VisualGraphValueType::Void},
        kb::visual::VisualGraphPin{.nodeId = 11, .direction = kb::visual::VisualGraphPinDirection::Input, .name = "value", .type = kb::visual::VisualGraphValueType::Float},
    };
    graph.edges = {
        kb::visual::VisualGraphEdge{.fromNode = 1, .fromPin = "then", .toNode = 10, .toPin = "exec"},
        kb::visual::VisualGraphEdge{.fromNode = 10, .fromPin = "then", .toNode = 11, .toPin = "exec"},
        kb::visual::VisualGraphEdge{.fromNode = 10, .fromPin = "value", .toNode = 11, .toPin = "value", .kind = kb::visual::VisualGraphEdgeKind::Data},
    };

    const kb::visual::VisualGraphCompileResult compiled = kb::visual::VisualGraphCompiler::Compile(graph);
    kb::tests::Require(compiled.Succeeded(), "Visual graph compiler rejected the executed data producer regression graph");
    kb::visual::VisualGraphRuntimeArtifact artifact{
        .assetId = kb::assets::AssetId{2U},
        .graphName = graph.name,
        .module = compiled.module,
    };

    int producerRuns = 0;
    float consumed = 0.0F;
    kb::visual::VisualGraphRuntimeBindingRegistry bindings;
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "Value",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [&producerRuns](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               ++producerRuns;
                               context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{3.0F});
                           },
                       }),
        "Visual graph runtime producer binding registration failed");
    kb::tests::Require(bindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "UseValue",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [&consumed](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
                               consumed = context.ReadFloat(instruction.inputs[0].sourceNodeId, instruction.inputs[0].sourcePin);
                           },
                       }),
        "Visual graph runtime consumer binding registration failed");

    kb::visual::VisualGraphRuntimeExecutionContext context;
    const kb::visual::VisualGraphRuntimeExecutor executor{bindings};
    const kb::visual::VisualGraphRuntimeExecutionResult executed = executor.Execute(artifact, kb::visual::VisualGraphLifecycleEvent::Tick, context);
    kb::tests::Require(executed.Succeeded(), "Visual graph runtime executor rejected the executed data producer regression graph");
    kb::tests::Require(producerRuns == 1, "Visual graph runtime executor reevaluated an already executed data producer");
    kb::tests::Require(consumed == 3.0F, "Visual graph runtime executor lost data from an executed producer node");

    const kb::visual::VisualGraphNativeCode generated = kb::visual::VisualGraphNativeCodeGenerator::Generate(compiled.module, kb::visual::VisualGraphNativeCodegenDesc{
        .className = "ExecutedDataProducerGraph",
        .namespaceName = "kb::game",
    });
    kb::tests::Require(generated.Succeeded(), "Visual graph native codegen rejected the executed data producer regression graph");
    kb::tests::Require(generated.source.find("context.IsNodeEvaluated(10U)") != std::string::npos, "Visual graph native codegen did not guard executed data producers");
    kb::tests::Require(generated.source.find("context.MarkNodeEvaluated(10U)") != std::string::npos, "Visual graph native codegen did not mark executed data producers");
}

void RunVisualGraphNodeDefinitionRegistryTest() {
    const kb::visual::VisualGraphNodeDefinitionRegistry registry = kb::visual::VisualGraphNodeDefinitionRegistry::CreateDefault();
    kb::tests::Require(registry.Definitions().size() == 10U, "Visual graph default node registry is incomplete");

    const kb::visual::VisualGraphNodeDefinition* branch = registry.Find(kb::visual::VisualGraphNodeKind::Branch);
    kb::tests::Require(branch != nullptr, "Visual graph node registry did not expose Branch");
    kb::tests::Require(branch->pins.size() == 4U, "Visual graph Branch definition did not expose expected pins");

    const std::vector<kb::visual::VisualGraphPin> callPins = registry.CreatePinsForNode(kb::visual::VisualGraphNode{
        .id = 77U,
        .kind = kb::visual::VisualGraphNodeKind::CallNative,
        .symbol = "DoWork",
    });
    kb::tests::Require(callPins.size() == 3U, "Visual graph node registry did not create pins for CallNative");
    kb::tests::Require(callPins[0].nodeId == 77U, "Visual graph node registry created pins for the wrong node");
    bool hasFailedPin = false;
    for (const kb::visual::VisualGraphPin& pin : callPins) {
        hasFailedPin = hasFailedPin || (pin.name == "failed" && pin.direction == kb::visual::VisualGraphPinDirection::Output);
    }
    kb::tests::Require(hasFailedPin, "LIB-061: CallNative must expose a \"failed\" exec output pin alongside \"then\"");
}

void RunVisualGraphNodeCatalogTest() {
    kb::visual::VisualGraphNativeBindingRegistry nativeBindings;
    kb::tests::Require(!nativeBindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "InvalidVoidInput",
                           .functionName = "InvalidVoidInput",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Void},
                           },
                       }),
        "Visual graph native binding registry accepted a Void data input");
    kb::tests::Require(!nativeBindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "InvalidDuplicateInput",
                           .functionName = "InvalidDuplicateInput",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                       }),
        "Visual graph native binding registry accepted duplicate input pins");
    kb::tests::Require(nativeBindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "MovePlayer",
                           .functionName = "NativeMovePlayer",
                           .inputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "delta", .type = kb::visual::VisualGraphValueType::Float},
                           },
                       }),
        "Visual graph node catalog native binding registration failed");
    kb::tests::Require(nativeBindings.Register(kb::visual::VisualGraphNativeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
                           .symbol = "DeltaSeconds",
                           .functionName = "NativeDeltaSeconds",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                       }),
        "Visual graph node catalog property binding registration failed");

    const kb::visual::VisualGraphNodeCatalog catalog = kb::visual::VisualGraphNodeCatalog::FromNativeBindings(nativeBindings);
    kb::tests::Require(catalog.Entries().size() == 12U, "Visual graph node catalog did not include built-in and native nodes");

    const kb::visual::VisualGraphNodeCatalogEntry* movePlayer = catalog.Find("NativeBinding:CallNative:MovePlayer");
    kb::tests::Require(movePlayer != nullptr, "Visual graph node catalog did not expose the native MovePlayer node");
    kb::tests::Require(movePlayer->kind == kb::visual::VisualGraphNodeKind::CallNative, "Visual graph node catalog assigned the wrong node kind");
    kb::tests::Require(movePlayer->pins.size() == 3U, "Visual graph node catalog did not create exec/input/then pins for native call");
    kb::tests::Require(movePlayer->pins[1].name == "delta" && movePlayer->pins[1].type == kb::visual::VisualGraphValueType::Float,
        "Visual graph node catalog did not preserve native input signature");

    const kb::visual::VisualGraphNodeCatalogEntry* deltaSeconds = catalog.Find("NativeBinding:GetProperty:DeltaSeconds");
    kb::tests::Require(deltaSeconds != nullptr, "Visual graph node catalog did not expose the native DeltaSeconds property node");
    kb::tests::Require(deltaSeconds->pins.size() == 1U && deltaSeconds->pins[0].name == "value", "Visual graph node catalog did not preserve property output signature");

    kb::visual::VisualGraphDocument document;
    const std::uint32_t nodeId = document.AddNode(*movePlayer);
    kb::tests::Require(document.Graph().FindNode(nodeId) != nullptr, "Visual graph document did not add a catalog node");
    kb::tests::Require(document.Graph().FindPin(nodeId, "delta", kb::visual::VisualGraphPinDirection::Input) != nullptr,
        "Visual graph document did not add catalog node input pins");
    kb::tests::Require(document.Graph().FindPin(nodeId, "then", kb::visual::VisualGraphPinDirection::Output) != nullptr,
        "Visual graph document did not add catalog node execution output");

    kb::visual::VisualGraphRuntimeBindingRegistry runtimeBindings;
    kb::tests::Require(!runtimeBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "InvalidEmptyOutput",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext&, const kb::visual::VisualGraphIrInstruction&) {},
                       }),
        "Visual graph runtime binding registry accepted an empty output pin name");
    kb::tests::Require(!runtimeBindings.Register(kb::visual::VisualGraphRuntimeBinding{
                           .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
                           .symbol = "InvalidDuplicateOutput",
                           .outputs = {
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                               kb::visual::VisualGraphNativePinSignature{.name = "value", .type = kb::visual::VisualGraphValueType::Float},
                           },
                           .callback = [](kb::visual::VisualGraphRuntimeExecutionContext&, const kb::visual::VisualGraphIrInstruction&) {},
                       }),
        "Visual graph runtime binding registry accepted duplicate output pins");
}

void RunVisualGraphDocumentEditingTest() {
    const kb::visual::VisualGraphNodeDefinitionRegistry definitions = kb::visual::VisualGraphNodeDefinitionRegistry::CreateDefault();
    kb::visual::VisualGraphDocument document;
    kb::visual::VisualGraphAsset& graph = document.MutableGraph();
    graph.name = "DocumentGraph";

    const std::uint32_t eventNode = document.AddNode(kb::visual::VisualGraphAddNodeDesc{
        .kind = kb::visual::VisualGraphNodeKind::Event,
        .lifecycle = kb::visual::VisualGraphLifecycleEvent::Tick,
    }, definitions);
    const std::uint32_t callNode = document.AddNode(kb::visual::VisualGraphAddNodeDesc{
        .kind = kb::visual::VisualGraphNodeKind::CallNative,
        .symbol = "DoWork",
    }, definitions);

    kb::tests::Require(document.Graph().FindPin(eventNode, "then", kb::visual::VisualGraphPinDirection::Output) != nullptr,
        "Visual graph document did not create event pins from definitions");
    kb::tests::Require(document.Graph().FindPin(callNode, "exec", kb::visual::VisualGraphPinDirection::Input) != nullptr,
        "Visual graph document did not create call input pins from definitions");
    kb::tests::Require(!document.ConnectExecution(eventNode, "then", callNode, "missing"), "Visual graph document connected an execution edge to a missing pin");
    kb::tests::Require(!document.ConnectData(eventNode, "then", callNode, "exec"), "Visual graph document connected data through Void execution pins");
    kb::tests::Require(document.ConnectExecution(eventNode, "then", callNode, "exec"), "Visual graph document did not connect execution pins");
    kb::tests::Require(!document.ConnectExecution(eventNode, "then", callNode, "exec"), "Visual graph document allowed a duplicate execution edge");
    kb::tests::Require(document.Graph().edges.size() == 1U, "Visual graph document did not store the edge");
    kb::tests::Require(document.RemoveNode(callNode), "Visual graph document did not remove a node");
    kb::tests::Require(document.Graph().FindNode(callNode) == nullptr, "Visual graph document left a removed node behind");
    kb::tests::Require(document.Graph().edges.empty(), "Visual graph document left edges for a removed node");
}

void RunBehaviourComponentSceneApiTest() {
    kb::scene::Scene scene;
    const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
        .name = "Scripted Object",
    });

    kb::tests::Require(!scene.Components().Behaviours().Has(object.Entity()), "New scene object unexpectedly has a behaviour component");
    scene.Components().Behaviours().Set(object.Entity(), kb::scene::BehaviourComponent{
        .behaviourAssetId = 42U,
        .backend = kb::scene::BehaviourBackend::VisualGraph,
        .enabled = true,
    });

    kb::tests::Require(scene.Components().Behaviours().Has(object.Entity()), "Behaviour component was not assigned");

    const kb::scene::Scene& readOnlyScene = scene;
    const kb::scene::BehaviourComponent* queriedBehaviour = readOnlyScene.Components().Behaviours().TryGet(object.Entity());
    kb::tests::Require(queriedBehaviour != nullptr, "Behaviour component was not queryable");
    kb::tests::Require(queriedBehaviour->behaviourAssetId == 42U, "Behaviour component asset id was not stored");
    kb::tests::Require(queriedBehaviour->backend == kb::scene::BehaviourBackend::VisualGraph, "Behaviour component backend was not stored");

    std::size_t visitedBehaviours = 0;
    readOnlyScene.Components().Behaviours().ForEach([](kb::scene::SceneEntity entity, const kb::scene::BehaviourComponent& behaviour, void* context) {
        auto& visited = *static_cast<std::size_t*>(context);
        kb::tests::Require(entity.IsValid(), "Behaviour component iteration yielded an invalid entity");
        kb::tests::Require(behaviour.behaviourAssetId == 42U, "Behaviour component iteration yielded the wrong component");
        ++visited;
    }, &visitedBehaviours);
    kb::tests::Require(visitedBehaviours == 1U, "Behaviour component iteration did not visit the assigned component");

    kb::scene::BehaviourComponent* mutableBehaviour = scene.Components().Behaviours().TryGet(object.Entity());
    kb::tests::Require(mutableBehaviour != nullptr, "Mutable behaviour component was not queryable");
    mutableBehaviour->enabled = false;
    scene.Components().Behaviours().MarkModified(object.Entity());
    kb::tests::Require(!scene.Components().Behaviours().TryGet(object.Entity())->enabled, "Behaviour component mutation was not stored");

    scene.Components().Behaviours().Remove(object.Entity());
    kb::tests::Require(!scene.Components().Behaviours().Has(object.Entity()), "Behaviour component was not removed");
}

} // namespace

namespace kb::tests {

void RunVisualGraphTests() {
    RunVisualGraphAssetLoaderTest();
    RunVisualGraphCustomEventEntryTest();
    RunVisualGraphCompilerAndCodegenTest();
    RunVisualGraphCompileServicePreparesRuntimeArtifactTest();
    RunVisualGraphCompileCoordinatorTest();
    RunVisualGraphRuntimeExecutorTest();
    RunVisualGraphBehaviourLifecycleRunnerTest();
    RunVisualGraphCompilerRejectsInvalidEdgesTest();
    RunVisualGraphCompilerRejectsInvalidDataPinsTest();
    RunVisualGraphCompilerRejectsCyclesTest();
    RunVisualGraphRuntimeControlFlowLoopWatchdogTest();
    RunVisualGraphCompilerBranchTargetsTest();
    RunVisualGraphRuntimeDataDependencyDoesNotFollowExecutionTest();
    RunVisualGraphRuntimeDoesNotReevaluateExecutedDataProducerTest();
    RunVisualGraphNodeDefinitionRegistryTest();
    RunVisualGraphNodeCatalogTest();
    RunVisualGraphDocumentEditingTest();
    RunBehaviourComponentSceneApiTest();
}

} // namespace kb::tests
