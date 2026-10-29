#include "scene/GameplayGraphEditorState.hpp"
#include "scene/EditorPlayModeSelectionSnapshot.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/visual/VisualGraphLiteral.hpp"
#include "engine/visual/VisualGraphNativeCodeGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

namespace kb::editor {
using namespace kb::visual;
bool GameplayGraphEditorState::Load(kb::scene::Scene& source, kb::assets::AssetId id) {
    error.clear();
    const auto* metadata = source.Assets().Manager().Registry().Find(id);
    if (!metadata || metadata->type != "VisualGraph") { error = "Select a Visual Graph asset"; return false; }
    if (assetId == id && path == metadata->physicalPath) { openRequested = true; return true; }
    if (document.Dirty()) { error = "Save the current graph before opening another graph"; return false; }
    VisualGraphAuthoringDocument candidate;
    if (!candidate.Load(metadata->physicalPath, error)) return false;
    kb::script::ScriptRuntimeHost host(source);
    if (!host.Succeeded()) { error = "Graph authoring runtime could not initialize"; return false; }
    StopPreview(); document = std::move(candidate); assetId = id; path = metadata->physicalPath;
    catalog = host.CreateVisualGraphNodeCatalog(); nativeBindings = host.VisualGraphNativeBindings(); selected = 0; selectedEdge.reset(); wire.reset(); breakpoints.clear();
    diagnostics = document.Validate(); openRequested = true; ++revision; return true;
}
bool GameplayGraphEditorState::Save(kb::scene::Scene& source) {
    const auto* metadata = source.Assets().Manager().Registry().Find(assetId);
    if (!metadata || metadata->type != "VisualGraph" || metadata->physicalPath != path) {
        error = "The graph no longer belongs to the current project"; return false;
    }
    if (!document.Save(path, error)) return false;
    if (!source.Assets().Manager().RefreshAsset(assetId)) { error = "Graph was saved, but its runtime refresh failed"; return false; }
    ++revision; return true;
}
bool GameplayGraphEditorState::Validate() {
    error.clear(); const auto compiled = VisualGraphCompiler::Compile(document.Snapshot().graph);
    diagnostics = compiled.diagnostics;
    if (compiled.Succeeded()) {
        const auto code = VisualGraphNativeCodeGenerator::Generate(compiled.module, {.bindings = &nativeBindings, .requireNativeBindings = true});
        diagnostics.insert(diagnostics.end(), code.diagnostics.begin(), code.diagnostics.end());
    }
    if (VisualGraphDiagnostics::HasErrors(diagnostics)) { error = diagnostics.front().message; return false; }
    return true;
}
void GameplayGraphEditorState::Edited() {
    std::erase_if(breakpoints, [&](const auto node) { return !document.Snapshot().graph.FindPin(node, "exec", VisualGraphPinDirection::Input); });
    StopPreview(); diagnostics = document.Validate(); selectedEdge.reset(); wire.reset(); error.clear(); ++revision;
}
void GameplayGraphEditorState::StopPreview() {
    scripts_.reset(); preview_.reset(); owner_ = {}; values_.ClearFrameState(); debugger_ = {};
    playing = false; paused = false; visited.clear(); ++revision;
}
bool GameplayGraphEditorState::BuildPreview(kb::scene::Scene& source, const kb::project::ProjectDescriptor& project, kb::scene::SceneEntity owner) {
    error.clear();
    if (!source.Entities().IsAlive(owner)) { error = "Select a live graph owner in the hierarchy"; return false; }
    if (!Validate()) return false;
    try {
        auto scene = std::make_unique<kb::scene::Scene>(project, kb::scene::SceneMode::Runtime);
        scene->Assets().Manager().Mounts() = source.Assets().Manager().Mounts();
        for (const auto& metadata : source.Assets().Manager().Registry().All()) static_cast<void>(scene->Assets().Manager().RegisterAsset(metadata));
        if (!scene->Assets().Manager().PublishRuntimeAsset(assetId, std::make_shared<VisualGraphAsset>(document.Snapshot().graph))) {
            error = "Graph working copy could not be published to preview"; return false;
        }
        EditorPlayModeSelectionSnapshot hierarchy; hierarchy.CaptureAuthoredHierarchy(source);
        if (!kb::scene::SceneDocumentService::LoadIntoScene(*scene, kb::scene::SceneDocumentService::Capture(source, "Graph Preview"))) {
            error = "Graph preview could not copy the scene"; return false;
        }
        kb::scene::SceneEntity cloned{};
        for (const auto& remap : hierarchy.RecreatedEntities(*scene)) if (remap.from == owner) cloned = remap.to;
        if (!scene->Entities().IsAlive(cloned)) { error = "Graph preview owner could not be remapped"; return false; }
        auto scripts = std::make_unique<kb::script::ScriptRuntimeHost>(*scene);
        if (!scripts->Succeeded()) { error = "Graph preview runtime could not initialize"; return false; }
        const auto compiled = VisualGraphCompiler::Compile(document.Snapshot().graph);
        if (std::ranges::none_of(compiled.module.functions, [&](const auto& function) { return function.customEventName.empty() && function.event == event; })) {
            error = "The graph does not contain the selected lifecycle event"; return false;
        }
        const auto code = VisualGraphNativeCodeGenerator::Generate(compiled.module, {.bindings = &scripts->VisualGraphNativeBindings(), .requireNativeBindings = true});
        diagnostics = code.diagnostics;
        if (!code.Succeeded()) { error = code.errors.empty() ? "Graph binding validation failed" : code.errors.front(); return false; }
        StopPreview(); preview_ = std::move(scene); scripts_ = std::move(scripts); owner_ = cloned;
        debugger_.SetExecutionNodesOnly(true);
        artifact_ = {.assetId = assetId, .graphName = document.Snapshot().graph.name, .module = compiled.module};
        values_.Store(0, "self", VisualGraphRuntimeValue{owner_.Id(), VisualGraphValueType::Entity});
        values_.Store(0, "asset", VisualGraphRuntimeValue{assetId.value, VisualGraphValueType::Hash});
        values_.SetDebugNodeVisitor(this, [](void* state, std::uint32_t node) noexcept {
            auto& visited = static_cast<GameplayGraphEditorState*>(state)->visited;
            if (visited.size() < 4096) visited.push_back(node);
            return true;
        });
        preview_->Runtime().SetPlaying(true); playing = true; paused = false; return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}
bool GameplayGraphEditorState::Execute(float delta) {
    if (!preview_ || !playing || paused || !std::isfinite(delta) || delta < 0 || delta > 0.25F) return false;
    std::vector<VisualGraphDebugBreakpoint> points;
    const auto previousPause = debugger_.LastPause();
    for (const auto node : breakpoints) points.push_back({assetId, node, !previousPause.valid || previousPause.nodeId != node});
    // The continuation starts at the paused flow node. Skip its breakpoint for
    // this invocation and clear the old snapshot so an ordinary Wait is not
    // mistaken for another debugger pause. Stepping still skips its first node.
    if (previousPause.valid) debugger_.ClearPause();
    debugger_.SetBreakpoints(std::move(points)); visited.clear();
    if (delta > 0) static_cast<void>(preview_->Runtime().Update(delta));
    values_.Store(0, "deltaSeconds", VisualGraphRuntimeValue{delta});
    const VisualGraphRuntimeExecutor executor{scripts_->VisualGraphRuntimeBindings(), &debugger_};
    const auto result = executor.Execute(artifact_, event, values_);
    diagnostics = result.diagnostics;
    if (!result.Succeeded()) { error = result.errors.empty() ? "Graph runtime failed" : result.errors.front(); playing = false; }
    paused = result.suspended && debugger_.LastPause().valid;
    const bool frameEvent = event == VisualGraphLifecycleEvent::Tick || event == VisualGraphLifecycleEvent::FixedTick ||
        event == VisualGraphLifecycleEvent::LateTick || event == VisualGraphLifecycleEvent::BeforeRender || event == VisualGraphLifecycleEvent::AfterRender;
    if (!frameEvent && !result.suspended) playing = false;
    preview_->Runtime().SynchronizeTransforms(); ++revision;
    return true;
}
bool GameplayGraphEditorState::Step() {
    if (!preview_) { error = "Start a graph preview before stepping"; return false; }
    debugger_.RequestStepInto(); paused = false; playing = true;
    return Execute(0);
}
void GameplayGraphEditorState::Resume() { if (preview_) { debugger_.Resume(); paused = false; playing = true; } }
bool GameplayGraphEditorState::ToggleBreakpoint(std::uint32_t node) {
    const auto& graph = document.Snapshot().graph;
    if (!graph.FindPin(node, "exec", VisualGraphPinDirection::Input)) {
        error = "Breakpoints pause execution nodes; inspect their data pins after a step"; return false;
    }
    const auto found = std::ranges::find(breakpoints, node);
    if (found == breakpoints.end()) breakpoints.push_back(node); else breakpoints.erase(found);
    error.clear(); ++revision; return true;
}
std::string GameplayGraphEditorState::PinValue(std::uint32_t node, std::string_view pin, bool output) const {
    if (!preview_) return {};
    if (!output) for (const auto& edge : document.Snapshot().graph.edges) {
        if (edge.kind == VisualGraphEdgeKind::Data && edge.toNode == node && edge.toPin == pin) {
            const auto* value = values_.TryRead(edge.fromNode, edge.fromPin); return value ? VisualGraphLiteral::Display(*value) : std::string{};
        }
    }
    const auto* value = values_.TryRead(node, pin); return value ? VisualGraphLiteral::Display(*value) : std::string{};
}
} // namespace kb::editor
