#include "engine/script/ScriptRuntimeHost.hpp"

#include "script/ScriptFunctionDocumentation.hpp"

#include "engine/library/EngineLibraryModule.hpp"
#include "engine/platform/UserStorage.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
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
#include <algorithm>
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

class ScriptRuntimeHostSceneSystem final : public kb::scene::SceneSystem, public kb::core::IRuntimeStateProvider {
public:
    explicit ScriptRuntimeHostSceneSystem(std::shared_ptr<ScriptRuntimeHostState> state);
    ~ScriptRuntimeHostSceneSystem() override;
    [[nodiscard]] std::unique_ptr<kb::core::RuntimeStateSnapshot> CaptureRuntimeState(std::string& error) override;
    [[nodiscard]] bool ValidateRuntimeState(const kb::core::RuntimeStateSnapshot& state, std::string& error) const override;
    [[nodiscard]] bool RestoreRuntimeState(const kb::core::RuntimeStateSnapshot& state, std::string& error) override;

    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnFrameStart(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
    void ExecuteShutdown(kb::scene::Scene& scene, float deltaSeconds);
    [[nodiscard]] ScriptRuntimeSceneSystem& System() noexcept { return system_; }
    [[nodiscard]] bool RequiresFixedStep() const override { return system_.RequiresFixedStep(); }
    [[nodiscard]] kb::scene::SceneUpdatePhase UpdatePhase() const noexcept override { return system_.UpdatePhase(); }
    [[nodiscard]] kb::scene::SceneFixedUpdatePhase FixedUpdatePhase() const noexcept override { return system_.FixedUpdatePhase(); }

private:
    void CollectDiagnostics();
    void DisableFailingBehaviour(const ScriptDiagnostic& diagnostic);

    std::shared_ptr<ScriptRuntimeHostState> state_;
    ScriptRuntimeSceneSystem system_;
};

} // namespace

struct ScriptRuntimeHostState final : std::enable_shared_from_this<ScriptRuntimeHostState> {
    explicit ScriptRuntimeHostState(kb::scene::Scene& sceneRef)
        : scene(sceneRef)
        , assetPreparer(sceneRef.Assets().Manager(), luaRuntime, visualGraphs) {}

    struct RuntimeSnapshot;
    [[nodiscard]] bool ValidateCheckpointBoundary(std::string& error) const;
    [[nodiscard]] std::unique_ptr<kb::core::RuntimeStateSnapshot> CaptureRuntimeState(std::string& error);
    [[nodiscard]] bool ValidateRuntimeState(const kb::core::RuntimeStateSnapshot& state, std::string& error) const;
    [[nodiscard]] bool RestoreRuntimeState(const kb::core::RuntimeStateSnapshot& state, std::string& error);
    kb::scene::SceneSystemHandle sceneSystemHandle;
    kb::scene::Scene& scene;
    PucLuaScriptRuntime luaRuntime;
    kb::visual::VisualGraphRuntimeRegistry visualGraphs;
    kb::visual::VisualGraphRuntimeBindingRegistry visualGraphRuntimeBindings;
    kb::visual::VisualGraphNativeBindingRegistry visualGraphNativeBindings;
    kb::visual::VisualGraphBehaviourInstanceRegistry visualGraphInstances;
    kb::visual::VisualGraphDebugSession visualGraphDebugger;
    ScriptRuntime runtime;
    ScriptApiNameRegistry apiNames;
    std::unique_ptr<NativeScriptPluginManager> nativePlugins;
    ScriptRuntimeAssetPreparer assetPreparer;
    ScriptRuntimeFrameSettings frameSettings;
    ScriptExecutionBudgetSettings executionBudgetSettings;
    std::shared_ptr<kb::platform::UserStorage> userStorage;
    bool disableFailingBehaviours = false;
    NativeScriptBackend* nativeBackend = nullptr;
    LuaScriptBackend* luaBackend = nullptr;
    VisualGraphScriptBackend* visualGraphBackend = nullptr;
    // Back-pointer to the scene system installed by InstallSceneSystem (owned by
    // the scene runtime) so the host can drive its shutdown lifecycle (fire
    // Destroyed) on demand — e.g. when the editor stops play — without tearing
    // the system down. Set by the system's constructor, cleared by its destructor.
    ScriptRuntimeHostSceneSystem* installedSceneSystem = nullptr;
    // Per-frame script diagnostics (compile/behaviour errors) that
    // ExecuteFrame/PrepareScene would otherwise drop; drained by the host so
    // the editor can surface why a behaviour is not running. De-duplicated so a
    // recurring per-frame error is reported once, not every frame.
    std::vector<std::string> pendingSceneSystemDiagnostics;
    std::unordered_set<std::string> reportedSceneSystemDiagnostics;
};

struct ScriptRuntimeHostState::RuntimeSnapshot final : kb::core::RuntimeStateSnapshot {
    std::weak_ptr<ScriptRuntimeHostState> lifetime;
    kb::scene::SceneSystemHandle systemHandle;
    bool installed = false;
    ScriptSharedState shared;
    decltype(ScriptEventBus::subscriptions_) subscriptions;
    decltype(ScriptEventBus::deferredEmits_) deferred;
    decltype(ScriptEventBus::observations_) observations;
    std::vector<std::pair<std::shared_ptr<ScriptEventObservation>, std::uint64_t>> sequences;
    EventSubscriptionHandle nextHandle = 1U;
    ScriptEventBusTelemetrySnapshot telemetry;
    ScriptRuntimeFrameSettings frameSettings;
    ScriptExecutionBudgetSettings budget;
    decltype(ScriptRuntimeHostState::pendingSceneSystemDiagnostics) pendingDiagnostics;
    decltype(ScriptRuntimeHostState::reportedSceneSystemDiagnostics) reportedDiagnostics;
    bool disableFailing = false;
    decltype(ScriptRuntimeSceneSystem::lifecycleRecords_) lifecycle;
    ScriptRuntimeFrameSettings systemFrameSettings;
    float accumulator = 0.0F;
    std::uint64_t gameplaySequence = 0U;
    ScriptRuntimeExecutionResult lastResult;
    ScriptRuntimeAssetPrepareResult lastPrepareResult;
    std::vector<kb::scene::PendingCollisionEvent> collisionScratch;
    std::unique_ptr<kb::core::RuntimeStateSnapshot> lua;
    [[nodiscard]] std::size_t PayloadBytes() const noexcept override { return lua->PayloadBytes(); }
};

bool ScriptRuntimeHostState::ValidateCheckpointBoundary(std::string& error) const {
    const auto& bus = this->runtime.Events();
    if (bus.emitDepth_ != 0U) { error = "Script host checkpoint requires an idle event bus"; return false; }
    bool nonLua = false;
    this->scene.Components().Behaviours().ForEach([](kb::scene::SceneEntity,
        const kb::scene::BehaviourComponent& behaviour, void* data) {
        if (behaviour.backend != kb::scene::BehaviourBackend::Lua) *static_cast<bool*>(data) = true;
    }, &nonLua);
    if (nonLua) { error = "Script host checkpoint requires state providers for non-Lua behaviours"; return false; }
    for (const auto& subscription : bus.subscriptions_) {
        bool managed = false;
        for (const auto& [key, handles] : this->luaRuntime.eventSubscriptionHandles_) {
            static_cast<void>(key);
            managed = managed || std::find(handles.begin(), handles.end(), subscription.handle) != handles.end();
        }
        if (!managed) { error = "Script host checkpoint cannot capture external native event callback state"; return false; }
    }
    error.clear();
    return true;
}

std::unique_ptr<kb::core::RuntimeStateSnapshot> ScriptRuntimeHostState::CaptureRuntimeState(std::string& error) {
    if (!ValidateCheckpointBoundary(error)) return {};
    auto snapshot = std::make_unique<RuntimeSnapshot>();
    snapshot->lua = this->luaRuntime.CaptureRuntimeStateImpl(error, true);
    if (!snapshot->lua) return {};
    snapshot->lifetime = shared_from_this();
    snapshot->systemHandle = sceneSystemHandle;
    snapshot->installed = this->installedSceneSystem != nullptr;
    snapshot->shared = this->runtime.SharedState();
    const auto& bus = this->runtime.Events();
    snapshot->subscriptions = bus.subscriptions_;
    snapshot->deferred = bus.deferredEmits_;
    snapshot->observations = bus.observations_;
    for (const auto& [name, recipients] : bus.observations_) {
        static_cast<void>(name);
        for (const auto& [recipient, weak] : recipients) {
            static_cast<void>(recipient);
            if (auto observation = weak.lock()) snapshot->sequences.emplace_back(observation, observation->sequence_);
        }
    }
    snapshot->nextHandle = bus.nextHandle_;
    snapshot->telemetry = bus.telemetry_;
    snapshot->frameSettings = this->frameSettings;
    snapshot->budget = this->executionBudgetSettings;
    snapshot->pendingDiagnostics = this->pendingSceneSystemDiagnostics;
    snapshot->reportedDiagnostics = this->reportedSceneSystemDiagnostics;
    snapshot->disableFailing = this->disableFailingBehaviours;
    if (snapshot->installed) {
        const auto& system = this->installedSceneSystem->System();
        snapshot->lifecycle = system.lifecycleRecords_;
        snapshot->systemFrameSettings = system.frameSettings_;
        snapshot->accumulator = system.fixedAccumulatorSeconds_;
        snapshot->gameplaySequence = system.lastGameplayEventSequence_;
        snapshot->lastResult = system.lastResult_;
        snapshot->lastPrepareResult = system.lastPrepareResult_;
        snapshot->collisionScratch = system.collisionEventScratch_;
    }
    error.clear();
    return snapshot;
}

bool ScriptRuntimeHostState::ValidateRuntimeState(const kb::core::RuntimeStateSnapshot& state, std::string& error) const {
    const auto* snapshot = dynamic_cast<const RuntimeSnapshot*>(&state);
    if (snapshot == nullptr || snapshot->lifetime.lock().get() != this) {
        error = "Script host snapshot belongs to another host lifetime"; return false;
    }
    if (snapshot->systemHandle != sceneSystemHandle || snapshot->installed != (this->installedSceneSystem != nullptr)) {
        error = "Script host scene-system lifetime changed since capture"; return false;
    }
    return ValidateCheckpointBoundary(error) && this->luaRuntime.ValidateRuntimeStateImpl(*snapshot->lua, error, true);
}

bool ScriptRuntimeHostState::RestoreRuntimeState(const kb::core::RuntimeStateSnapshot& state, std::string& error) {
    if (!ValidateRuntimeState(state, error)) return false;
    const auto& snapshot = static_cast<const RuntimeSnapshot&>(state);
    // Copy callback owners before destroying the future branch. Their registry
    // references are restored by the heap image, never recreated in another slot.
    auto subscriptions = snapshot.subscriptions;
    auto deferred = snapshot.deferred;
    auto observations = snapshot.observations;
    auto shared = snapshot.shared;
    auto& bus = this->runtime.Events();
    bus.subscriptions_.clear();
    if (!this->luaRuntime.RestoreRuntimeStateImpl(*snapshot.lua, error, true)) return false;
    bus.subscriptions_ = std::move(subscriptions);
    bus.deferredEmits_ = std::move(deferred);
    bus.observations_ = std::move(observations);
    for (const auto& [observation, sequence] : snapshot.sequences) observation->sequence_ = sequence;
    bus.nextHandle_ = std::max(bus.nextHandle_, snapshot.nextHandle);
    bus.telemetry_ = snapshot.telemetry;
    this->runtime.SharedState() = std::move(shared);
    this->frameSettings = snapshot.frameSettings;
    this->executionBudgetSettings = snapshot.budget;
    this->pendingSceneSystemDiagnostics = snapshot.pendingDiagnostics;
    this->reportedSceneSystemDiagnostics = snapshot.reportedDiagnostics;
    this->disableFailingBehaviours = snapshot.disableFailing;
    if (snapshot.installed) {
        auto& system = this->installedSceneSystem->System();
        system.lifecycleRecords_ = snapshot.lifecycle;
        system.frameSettings_ = snapshot.systemFrameSettings;
        system.fixedAccumulatorSeconds_ = snapshot.accumulator;
        system.lastGameplayEventSequence_ = snapshot.gameplaySequence;
        system.lastResult_ = snapshot.lastResult;
        system.lastPrepareResult_ = snapshot.lastPrepareResult;
        system.collisionEventScratch_ = snapshot.collisionScratch;
    }
    error.clear();
    return true;
}

namespace {

ScriptRuntimeHostSceneSystem::ScriptRuntimeHostSceneSystem(std::shared_ptr<ScriptRuntimeHostState> state)
    : state_(std::move(state))
    , system_(state_->runtime, state_->assetPreparer) {
    system_.SetFrameSettings(state_->frameSettings);
    state_->installedSceneSystem = this;
}

std::unique_ptr<kb::core::RuntimeStateSnapshot> ScriptRuntimeHostSceneSystem::CaptureRuntimeState(std::string& error) {
    return state_->CaptureRuntimeState(error);
}

bool ScriptRuntimeHostSceneSystem::ValidateRuntimeState(const kb::core::RuntimeStateSnapshot& snapshot, std::string& error) const {
    return state_->ValidateRuntimeState(snapshot, error);
}

bool ScriptRuntimeHostSceneSystem::RestoreRuntimeState(const kb::core::RuntimeStateSnapshot& snapshot, std::string& error) {
    return state_->RestoreRuntimeState(snapshot, error);
}

ScriptRuntimeHostSceneSystem::~ScriptRuntimeHostSceneSystem() {
    if (state_ != nullptr && state_->installedSceneSystem == this) {
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
    CollectDiagnostics();
}

void ScriptRuntimeHostSceneSystem::ExecuteShutdown(kb::scene::Scene& scene, float deltaSeconds) {
    static_cast<void>(system_.ExecuteShutdown(scene, deltaSeconds));
    CollectDiagnostics();
}

// The script file a behaviour refers to, or why there is none: a reference to a moved, renamed or
// deleted file has no asset behind it.
[[nodiscard]] std::string ScriptName(const kb::scene::Scene& scene, kb::assets::AssetId assetId) {
    if (const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(assetId); metadata != nullptr) {
        return metadata->virtualPath.generic_string();
    }
    return "a missing script (id " + kb::assets::ToString(assetId) +
        "; the file was moved, renamed or deleted - attach the script to the object again)";
}

[[nodiscard]] std::string EntityName(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) {
    const std::string name = scene.Entities().Name(entity);
    return name.empty() ? "entity #" + std::to_string(entity.Id()) : "'" + name + "'";
}

void ScriptRuntimeHostSceneSystem::CollectDiagnostics() {
    const auto push = [this](std::string line) {
        if (state_->reportedSceneSystemDiagnostics.insert(line).second) {
            state_->pendingSceneSystemDiagnostics.push_back(std::move(line));
        }
    };
    for (const ScriptDiagnostic& diagnostic : system_.LastResult().diagnostics) {
        // Name the object and its script so a broken behaviour (e.g. a dangling reference to a
        // deleted/renamed script - "lua script is not loaded") points at exactly which object's
        // Script component to fix, instead of a bare, un-actionable number.
        push(EntityName(state_->scene, diagnostic.entity) + ", " + ScriptName(state_->scene, diagnostic.assetId) + ": " +
            diagnostic.message +
            (diagnostic.stackTrace.empty() ? std::string{} : "\n" + diagnostic.stackTrace));
        if (state_->disableFailingBehaviours) {
            DisableFailingBehaviour(diagnostic);
        }
    }
    for (const ScriptRuntimeAssetPrepareDiagnostic& diagnostic : system_.LastPrepareResult().diagnostics) {
        push(ScriptName(state_->scene, diagnostic.assetId) + " could not be loaded: " + diagnostic.message);
    }
}

void ScriptRuntimeHostSceneSystem::DisableFailingBehaviour(const ScriptDiagnostic& diagnostic) {
    kb::scene::SceneBehaviourComponents behaviours = state_->scene.Components().Behaviours();
    const kb::scene::BehaviourComponent* current = behaviours.TryGet(diagnostic.entity);
    if (current == nullptr || !current->enabled || current->behaviourAssetId != diagnostic.assetId.value) {
        return;
    }
    kb::scene::BehaviourComponent disabled = *current;
    disabled.enabled = false;
    behaviours.Set(diagnostic.entity, disabled);
    state_->pendingSceneSystemDiagnostics.push_back(EntityName(state_->scene, diagnostic.entity) + ", " +
        ScriptName(state_->scene, diagnostic.assetId) + ": the script was disabled after its error");
}

} // namespace

ScriptRuntimeHost::ScriptRuntimeHost(kb::scene::Scene& scene, ScriptRuntimeHostOptions options)
    : state_(std::make_shared<ScriptRuntimeHostState>(scene)) {
    if (options.visualGraphPrepareSettings.nativeBindings == nullptr) {
        options.visualGraphPrepareSettings.nativeBindings = &state_->visualGraphNativeBindings;
    }
    state_->frameSettings = options.frameSettings;
    state_->executionBudgetSettings = options.executionBudgetSettings;
    state_->disableFailingBehaviours = options.disableFailingBehaviours;
    state_->luaRuntime.SetExecutionBudgetSettings(options.executionBudgetSettings);
    if (!options.userStorageRoot.empty()) {
        state_->userStorage = std::make_shared<kb::platform::UserStorage>(
            std::move(options.userStorageRoot), options.userStorageQuotaBytes);
    }
    state_->assetPreparer.SetVisualGraphSettings(std::move(options.visualGraphPrepareSettings));
    state_->assetPreparer.SetNativeSettings(std::move(options.nativePrepareSettings));
    RegisterDefaultBackends();
    if (options.installSceneSystem) {
        static_cast<void>(InstallSceneSystem());
    }
}

ScriptRuntimeHost::~ScriptRuntimeHost() {
    if (state_->installedSceneSystem != nullptr && state_->sceneSystemHandle.IsValid() &&
        state_->scene.Runtime().HasSceneSystem(state_->sceneSystemHandle)) {
        static_cast<void>(state_->scene.Runtime().RemoveSceneSystem(state_->sceneSystemHandle));
    }
}

std::unique_ptr<kb::core::RuntimeStateSnapshot> ScriptRuntimeHost::CaptureRuntimeState(std::string& error) {
    return state_->CaptureRuntimeState(error);
}

bool ScriptRuntimeHost::ValidateRuntimeState(const kb::core::RuntimeStateSnapshot& snapshot, std::string& error) const {
    return state_->ValidateRuntimeState(snapshot, error);
}

bool ScriptRuntimeHost::RestoreRuntimeState(const kb::core::RuntimeStateSnapshot& snapshot, std::string& error) {
    return state_->RestoreRuntimeState(snapshot, error);
}

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

const ScriptRuntimeExecutionResult* ScriptRuntimeHost::InstalledSceneSystemLastResult() const noexcept {
    return state_->installedSceneSystem == nullptr ? nullptr : &state_->installedSceneSystem->System().LastResult();
}

const ScriptRuntimeAssetPrepareResult* ScriptRuntimeHost::InstalledSceneSystemLastPrepareResult() const noexcept {
    return state_->installedSceneSystem == nullptr ? nullptr : &state_->installedSceneSystem->System().LastPrepareResult();
}

const std::vector<kb::library::EngineLibraryModuleReportEntry>& ScriptRuntimeHost::LibraryStartupReport() const noexcept {
    return libraryStartupReport_;
}

bool ScriptRuntimeHost::InstallSceneSystem() {
    if (state_->installedSceneSystem != nullptr) {
        return true;
    }
    if (!Succeeded()) {
        return false;
    }
    state_->sceneSystemHandle = state_->scene.Runtime().AddSceneSystem(std::make_unique<ScriptRuntimeHostSceneSystem>(state_));
    return state_->sceneSystemHandle.IsValid();
}

bool ScriptRuntimeHost::DispatchShutdownLifecycle(float deltaSeconds) {
    if (state_->installedSceneSystem == nullptr) {
        return false;
    }
    state_->installedSceneSystem->ExecuteShutdown(state_->scene, deltaSeconds);
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

std::shared_ptr<kb::platform::UserStorage> ScriptRuntimeHost::UserStorage() const noexcept {
    return state_->userStorage;
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

kb::visual::VisualGraphDebugSession& ScriptRuntimeHost::VisualGraphDebugger() noexcept {
    return state_->visualGraphDebugger;
}

const kb::visual::VisualGraphDebugSession& ScriptRuntimeHost::VisualGraphDebugger() const noexcept {
    return state_->visualGraphDebugger;
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

    auto visualGraphBackend = std::make_unique<VisualGraphScriptBackend>(
        state_->visualGraphs,
        state_->visualGraphRuntimeBindings,
        state_->visualGraphInstances,
        &state_->visualGraphDebugger,
        state_->executionBudgetSettings);
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
