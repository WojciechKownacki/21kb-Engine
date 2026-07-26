#include "engine/script/ScriptRuntimeHost.hpp"

#include "script/ScriptFunctionDocumentation.hpp"

#include "engine/library/EngineLibraryModule.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/script/ScriptRuntimeSceneSystem.hpp"
#include "engine/script/ScriptBackend.hpp"
#include "engine/script/ScriptRuntimeAssetPreparer.hpp"
#include "engine/script/ScriptFunctionVisualGraphBindings.hpp"
#include "engine/script/ScriptSceneVisualGraphBindings.hpp"
#include "engine/script/ScriptSharedVisualGraphBindings.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

[[nodiscard]] std::vector<ScriptApiPin> ToApiPins(const std::vector<ScriptFunctionPin>& pins) {
    std::vector<ScriptApiPin> apiPins;
    apiPins.reserve(pins.size());
    for (const ScriptFunctionPin& pin : pins) {
        apiPins.push_back(ScriptApiPin{
            .name = pin.name,
            .type = pin.type,
            .required = pin.required,
        });
    }
    return apiPins;
}

class ScriptRuntimeHostSceneSystem final : public kb::scene::SceneSystem {
public:
    explicit ScriptRuntimeHostSceneSystem(std::shared_ptr<ScriptRuntimeHostState> state);
    ~ScriptRuntimeHostSceneSystem() override;

    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnFrameStart(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
    [[nodiscard]] bool RequiresFixedStep() const override { return system_.RequiresFixedStep(); }
    [[nodiscard]] kb::scene::SceneUpdatePhase UpdatePhase() const noexcept override { return system_.UpdatePhase(); }
    [[nodiscard]] kb::scene::SceneFixedUpdatePhase FixedUpdatePhase() const noexcept override { return system_.FixedUpdatePhase(); }

private:
    void CollectDiagnostics();

    std::shared_ptr<ScriptRuntimeHostState> state_;
    ScriptRuntimeSceneSystem system_;
};

} // namespace

struct ScriptRuntimeHostState final {
    explicit ScriptRuntimeHostState(kb::scene::Scene& sceneRef)
        : scene(sceneRef)
        , assetPreparer(sceneRef.Assets().Manager(), luaRuntime, visualGraphs) {}

    kb::scene::Scene& scene;
    PucLuaScriptRuntime luaRuntime;
    kb::visual::VisualGraphRuntimeRegistry visualGraphs;
    kb::visual::VisualGraphRuntimeBindingRegistry visualGraphRuntimeBindings;
    kb::visual::VisualGraphNativeBindingRegistry visualGraphNativeBindings;
    kb::visual::VisualGraphBehaviourInstanceRegistry visualGraphInstances;
    ScriptRuntime runtime;
    ScriptApiNameRegistry apiNames;
    std::unique_ptr<NativeScriptPluginManager> nativePlugins;
    ScriptRuntimeAssetPreparer assetPreparer;
    ScriptRuntimeFrameSettings frameSettings;
    NativeScriptBackend* nativeBackend = nullptr;
    LuaScriptBackend* luaBackend = nullptr;
    VisualGraphScriptBackend* visualGraphBackend = nullptr;
    // Back-pointer to the scene system installed by InstallSceneSystem (owned by
    // the scene runtime) so the host can drive its shutdown lifecycle (fire
    // Destroyed) on demand — e.g. when the editor stops play — without tearing
    // the system down. Set by the system's constructor, cleared by its destructor.
    ScriptRuntimeSceneSystem* installedSceneSystem = nullptr;
    // Per-frame script diagnostics (compile/behaviour errors) that
    // ExecuteFrame/PrepareScene would otherwise drop; drained by the host so
    // the editor can surface why a behaviour is not running. De-duplicated so a
    // recurring per-frame error is reported once, not every frame.
    std::vector<std::string> pendingSceneSystemDiagnostics;
    std::unordered_set<std::string> reportedSceneSystemDiagnostics;
};

namespace {

ScriptRuntimeHostSceneSystem::ScriptRuntimeHostSceneSystem(std::shared_ptr<ScriptRuntimeHostState> state)
    : state_(std::move(state))
    , system_(state_->runtime, state_->assetPreparer) {
    system_.SetFrameSettings(state_->frameSettings);
    state_->installedSceneSystem = &system_;
}

ScriptRuntimeHostSceneSystem::~ScriptRuntimeHostSceneSystem() {
    if (state_ != nullptr && state_->installedSceneSystem == &system_) {
        state_->installedSceneSystem = nullptr;
    }
}

void ScriptRuntimeHostSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    system_.OnCreate(context);
    CollectDiagnostics();
}

void ScriptRuntimeHostSceneSystem::OnFrameStart(kb::scene::SceneSystemContext& context) {
    system_.OnFrameStart(context);
    CollectDiagnostics();
}

void ScriptRuntimeHostSceneSystem::OnUpdate(kb::scene::SceneSystemContext& context) {
    system_.OnUpdate(context);
    CollectDiagnostics();
}

void ScriptRuntimeHostSceneSystem::OnFixedUpdate(kb::scene::SceneSystemContext& context) {
    system_.OnFixedUpdate(context);
    CollectDiagnostics();
}

void ScriptRuntimeHostSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    system_.OnDestroy(context);
}

void ScriptRuntimeHostSceneSystem::CollectDiagnostics() {
    const auto push = [this](std::string line) {
        if (state_->reportedSceneSystemDiagnostics.insert(line).second) {
            state_->pendingSceneSystemDiagnostics.push_back(std::move(line));
        }
    };
    for (const ScriptDiagnostic& diagnostic : system_.LastResult().diagnostics) {
        // Name the entity and the referenced script asset so a broken behaviour
        // (e.g. a dangling reference to a deleted/renamed script — "lua script
        // is not loaded") points at exactly which object's Script component to
        // fix, instead of a bare, un-actionable message.
        push("behaviour error: " + diagnostic.message
            + " (entity #" + std::to_string(diagnostic.entity.Id())
            + ", script asset #" + std::to_string(diagnostic.assetId.value) + ")");
    }
    for (const ScriptRuntimeAssetPrepareDiagnostic& diagnostic : system_.LastPrepareResult().diagnostics) {
        push("behaviour could not load/compile: " + diagnostic.message
            + " (script asset #" + std::to_string(diagnostic.assetId.value) + ")");
    }
}

} // namespace

ScriptRuntimeHost::ScriptRuntimeHost(kb::scene::Scene& scene, ScriptRuntimeHostOptions options)
    : state_(std::make_shared<ScriptRuntimeHostState>(scene)) {
    if (options.visualGraphPrepareSettings.nativeBindings == nullptr) {
        options.visualGraphPrepareSettings.nativeBindings = &state_->visualGraphNativeBindings;
    }
    state_->frameSettings = options.frameSettings;
    state_->assetPreparer.SetVisualGraphSettings(std::move(options.visualGraphPrepareSettings));
    state_->assetPreparer.SetNativeSettings(std::move(options.nativePrepareSettings));
    RegisterDefaultBackends();
    if (options.installSceneSystem) {
        static_cast<void>(InstallSceneSystem());
    }
}

ScriptRuntimeHost::~ScriptRuntimeHost() = default;

bool ScriptRuntimeHost::Succeeded() const noexcept {
    return diagnostics_.empty();
}

const std::vector<std::string>& ScriptRuntimeHost::Diagnostics() const noexcept {
    return diagnostics_;
}

std::vector<std::string> ScriptRuntimeHost::DrainSceneSystemDiagnostics() {
    std::vector<std::string> drained;
    drained.swap(state_->pendingSceneSystemDiagnostics);
    return drained;
}

const std::vector<kb::library::EngineLibraryModuleReportEntry>& ScriptRuntimeHost::LibraryStartupReport() const noexcept {
    return libraryStartupReport_;
}

bool ScriptRuntimeHost::InstallSceneSystem() {
    if (sceneSystemInstalled_) {
        return true;
    }
    if (!Succeeded()) {
        return false;
    }
    state_->scene.Runtime().AddSceneSystem(std::make_unique<ScriptRuntimeHostSceneSystem>(state_));
    sceneSystemInstalled_ = true;
    return true;
}

bool ScriptRuntimeHost::DispatchShutdownLifecycle(float deltaSeconds) {
    if (state_->installedSceneSystem == nullptr) {
        return false;
    }
    static_cast<void>(state_->installedSceneSystem->ExecuteShutdown(state_->scene, deltaSeconds));
    return true;
}

kb::visual::VisualGraphNodeCatalog ScriptRuntimeHost::CreateVisualGraphNodeCatalog() const {
    kb::visual::VisualGraphNodeCatalog catalog = kb::visual::VisualGraphNodeCatalog::CreateDefault();
    catalog.RegisterNativeBindings(state_->visualGraphNativeBindings);
    catalog.RegisterRuntimeBindings(state_->visualGraphRuntimeBindings);
    return catalog;
}

ScriptRuntime& ScriptRuntimeHost::Runtime() noexcept {
    return state_->runtime;
}

const ScriptRuntime& ScriptRuntimeHost::Runtime() const noexcept {
    return state_->runtime;
}

ScriptSharedState& ScriptRuntimeHost::SharedState() noexcept {
    return state_->runtime.SharedState();
}

const ScriptSharedState& ScriptRuntimeHost::SharedState() const noexcept {
    return state_->runtime.SharedState();
}

ScriptFunctionRegistry& ScriptRuntimeHost::Functions() noexcept {
    return state_->runtime.Functions();
}

const ScriptFunctionRegistry& ScriptRuntimeHost::Functions() const noexcept {
    return state_->runtime.Functions();
}

ScriptApiNameRegistry& ScriptRuntimeHost::ApiNames() noexcept {
    return state_->apiNames;
}

const ScriptApiNameRegistry& ScriptRuntimeHost::ApiNames() const noexcept {
    return state_->apiNames;
}

bool ScriptRuntimeHost::RegisterFunction(ScriptFunctionDesc function) {
    const std::string functionName = function.signature.name;
    if (function.signature.description.empty()) {
        function.signature.description = BuiltInScriptFunctionDescription(functionName);
    }
    const std::string visualGraphSymbol = "Function." + functionName;
    if (functionName.empty() || function.signature.description.empty() ||
        state_->runtime.Functions().FindSignature(functionName) != nullptr ||
        state_->visualGraphRuntimeBindings.Find(kb::visual::VisualGraphIrOpcode::CallNative, visualGraphSymbol) != nullptr ||
        state_->visualGraphNativeBindings.Find(kb::visual::VisualGraphIrOpcode::CallNative, visualGraphSymbol) != nullptr) {
        return false;
    }
    const std::vector<ScriptApiPin> inputs = ToApiPins(function.signature.inputs);
    const std::vector<ScriptApiPin> outputs = ToApiPins(function.signature.outputs);
    if (!state_->runtime.Functions().Register(std::move(function))) {
        return false;
    }
    static_cast<void>(state_->apiNames.RegisterFunction(functionName, inputs, outputs, "ScriptRuntimeHost", true));
    const ScriptFunctionSignature* signature = state_->runtime.Functions().FindSignature(functionName);
    if (signature == nullptr) {
        return false;
    }
    return ScriptFunctionVisualGraphBindings::RegisterFunction(
        state_->visualGraphRuntimeBindings,
        state_->visualGraphNativeBindings,
        state_->runtime.Functions(),
        *signature,
        state_->scene);
}

ScriptRuntimeAssetPreparer& ScriptRuntimeHost::AssetPreparer() noexcept {
    return state_->assetPreparer;
}

const ScriptRuntimeAssetPreparer& ScriptRuntimeHost::AssetPreparer() const noexcept {
    return state_->assetPreparer;
}

PucLuaScriptRuntime& ScriptRuntimeHost::LuaRuntime() noexcept {
    return state_->luaRuntime;
}

const PucLuaScriptRuntime& ScriptRuntimeHost::LuaRuntime() const noexcept {
    return state_->luaRuntime;
}

NativeScriptBackend& ScriptRuntimeHost::NativeBackend() noexcept {
    return *state_->nativeBackend;
}

const NativeScriptBackend& ScriptRuntimeHost::NativeBackend() const noexcept {
    return *state_->nativeBackend;
}

NativeScriptPluginManager& ScriptRuntimeHost::NativePlugins() noexcept {
    return *state_->nativePlugins;
}

const NativeScriptPluginManager& ScriptRuntimeHost::NativePlugins() const noexcept {
    return *state_->nativePlugins;
}

VisualGraphScriptBackend& ScriptRuntimeHost::VisualGraphBackend() noexcept {
    return *state_->visualGraphBackend;
}

const VisualGraphScriptBackend& ScriptRuntimeHost::VisualGraphBackend() const noexcept {
    return *state_->visualGraphBackend;
}

kb::visual::VisualGraphRuntimeRegistry& ScriptRuntimeHost::VisualGraphs() noexcept {
    return state_->visualGraphs;
}

const kb::visual::VisualGraphRuntimeRegistry& ScriptRuntimeHost::VisualGraphs() const noexcept {
    return state_->visualGraphs;
}

kb::visual::VisualGraphRuntimeBindingRegistry& ScriptRuntimeHost::VisualGraphRuntimeBindings() noexcept {
    return state_->visualGraphRuntimeBindings;
}

const kb::visual::VisualGraphRuntimeBindingRegistry& ScriptRuntimeHost::VisualGraphRuntimeBindings() const noexcept {
    return state_->visualGraphRuntimeBindings;
}

kb::visual::VisualGraphNativeBindingRegistry& ScriptRuntimeHost::VisualGraphNativeBindings() noexcept {
    return state_->visualGraphNativeBindings;
}

const kb::visual::VisualGraphNativeBindingRegistry& ScriptRuntimeHost::VisualGraphNativeBindings() const noexcept {
    return state_->visualGraphNativeBindings;
}

kb::visual::VisualGraphBehaviourInstanceRegistry& ScriptRuntimeHost::VisualGraphInstances() noexcept {
    return state_->visualGraphInstances;
}

const kb::visual::VisualGraphBehaviourInstanceRegistry& ScriptRuntimeHost::VisualGraphInstances() const noexcept {
    return state_->visualGraphInstances;
}

void ScriptRuntimeHost::RegisterDefaultBackends() {
    auto nativeBackend = std::make_unique<NativeScriptBackend>();
    state_->nativeBackend = nativeBackend.get();
    if (!state_->runtime.RegisterBackend(std::move(nativeBackend))) {
        AddDiagnostic("native script backend could not be registered");
    } else {
        state_->nativePlugins = std::make_unique<NativeScriptPluginManager>(*state_->nativeBackend);
        state_->assetPreparer.SetNativePluginManager(*state_->nativePlugins);
        state_->assetPreparer.SetNativeBackend(*state_->nativeBackend);
    }

    auto luaBackend = std::make_unique<LuaScriptBackend>(state_->luaRuntime);
    state_->luaBackend = luaBackend.get();
    if (!state_->runtime.RegisterBackend(std::move(luaBackend))) {
        AddDiagnostic("Lua script backend could not be registered");
    }

    if (!ScriptSceneVisualGraphBindings::Register(state_->visualGraphRuntimeBindings, state_->scene)) {
        AddDiagnostic("VisualGraph scene component runtime bindings could not be registered");
    }
    if (!ScriptSceneVisualGraphBindings::RegisterNative(state_->visualGraphNativeBindings)) {
        AddDiagnostic("VisualGraph scene component native bindings could not be registered");
    }
    if (!ScriptSharedVisualGraphBindings::Register(state_->visualGraphRuntimeBindings, state_->runtime.SharedState())) {
        AddDiagnostic("VisualGraph shared state runtime bindings could not be registered");
    }
    if (!ScriptSharedVisualGraphBindings::RegisterNative(state_->visualGraphNativeBindings)) {
        AddDiagnostic("VisualGraph shared state native bindings could not be registered");
    }
    if (!ScriptFunctionVisualGraphBindings::Register(state_->visualGraphRuntimeBindings, state_->runtime.Functions(), state_->scene)) {
        AddDiagnostic("VisualGraph script function runtime bindings could not be registered");
    }
    if (!ScriptFunctionVisualGraphBindings::RegisterNative(state_->visualGraphNativeBindings, state_->runtime.Functions())) {
        AddDiagnostic("VisualGraph script function native bindings could not be registered");
    }

    auto visualGraphBackend = std::make_unique<VisualGraphScriptBackend>(state_->visualGraphs, state_->visualGraphRuntimeBindings, state_->visualGraphInstances);
    state_->visualGraphBackend = visualGraphBackend.get();
    if (!state_->runtime.RegisterBackend(std::move(visualGraphBackend))) {
        AddDiagnostic("VisualGraph script backend could not be registered");
    }

    // Engine21kbLibrary (namespace kb::library) is the single entry point for
    // the domain API surface (Input, Audio, World, Time, Physics, Transform,
    // ...). Install() registers each module in turn; every RegisterFunction
    // call it makes mirrors into the Lua function table and a Visual Graph
    // CallNative node, so this one call covers all three scripting backends.
    const kb::library::EngineLibraryModuleResult libraryResult = kb::library::EngineLibraryModule::Install(*this);
    for (const std::string& diagnostic : libraryResult.diagnostics) {
        AddDiagnostic(diagnostic);
    }
    libraryStartupReport_ = libraryResult.report;
}

void ScriptRuntimeHost::AddDiagnostic(std::string message) {
    diagnostics_.push_back(std::move(message));
}

} // namespace kb::script
