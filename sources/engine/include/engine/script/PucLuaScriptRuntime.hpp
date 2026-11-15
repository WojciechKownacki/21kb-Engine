#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/core/RuntimeState.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/LuaScriptBackend.hpp"
#include "engine/script/ScriptApiNameRegistry.hpp"
#include "engine/script/ScriptEventBus.hpp"
#include "engine/script/ScriptExecutionBudget.hpp"
#include "engine/script/ScriptValue.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace kb::script {

class PucLuaRunaway;
struct PucLuaMemoryBudget;
struct PucLuaStateLifetime;

using PucLuaLoadResult = LuaScriptLoadResult;

struct PucLuaDebugBreakpoint {
    std::string chunkName;
    int line = 0;
    bool enabled = true;
};

struct PucLuaDebugSettings {
    bool enableBreakpoints = true;
    bool stopOnBreakpoint = true;
    bool collectCallStack = true;
    bool collectLocals = true;
    std::vector<PucLuaDebugBreakpoint> breakpoints;
};

enum class PucLuaDebugPauseReason {
    Breakpoint,
    ManualBreak,
    Step,
};

struct PucLuaDebugVariableSnapshot {
    std::string name;
    std::string value;
    ScriptValueType type = ScriptValueType::Void;
};

struct PucLuaDebugEvaluationResult {
    bool succeeded = false;
    PucLuaDebugVariableSnapshot value;
    std::string error;
};

struct PucLuaDebugFrameSnapshot {
    std::string name;
    std::string chunkName;
    int line = 0;
    std::vector<PucLuaDebugVariableSnapshot> locals;
};

struct PucLuaDebugPauseSnapshot {
    bool valid = false;
    PucLuaDebugPauseReason reason = PucLuaDebugPauseReason::Breakpoint;
    std::string chunkName;
    int line = 0;
    std::vector<PucLuaDebugFrameSnapshot> callStack;
    // True when the script is suspended at this line until Continue or a step releases it. False when the pause
    // was only recorded: the line ran where Lua cannot yield (a chunk's top-level code, or Lua called back from C
    // such as a sort comparator), so execution went on without stopping and without an error.
    bool suspended = false;
};

struct PucLuaExposedVariableInstance {
    std::string name;
    ScriptValueType type = ScriptValueType::Void;
    ScriptValue value;
    bool overridden = false;
};

// What the collector's end-of-frame steps have cost so far (PucLuaScriptRuntime::StepGarbageCollector).
struct PucLuaCollectionStats {
    std::uint64_t steps = 0U;
    std::uint64_t totalMicroseconds = 0U;
    std::uint64_t longestStepMicroseconds = 0U;
};

class PucLuaScriptRuntime final : public ILuaScriptRuntime, public ILuaScriptAssetStore, public kb::core::IRuntimeStateProvider {
public:
    PucLuaScriptRuntime();
    ~PucLuaScriptRuntime() override;

    [[nodiscard]] std::unique_ptr<kb::core::RuntimeStateSnapshot> CaptureRuntimeState(std::string& error) override;
    [[nodiscard]] bool ValidateRuntimeState(const kb::core::RuntimeStateSnapshot& snapshot, std::string& error) const override;
    [[nodiscard]] bool RestoreRuntimeState(const kb::core::RuntimeStateSnapshot& snapshot, std::string& error) override;

    PucLuaScriptRuntime(const PucLuaScriptRuntime&) = delete;
    PucLuaScriptRuntime& operator=(const PucLuaScriptRuntime&) = delete;
    PucLuaScriptRuntime(PucLuaScriptRuntime&&) = delete;
    PucLuaScriptRuntime& operator=(PucLuaScriptRuntime&&) = delete;

    [[nodiscard]] PucLuaLoadResult LoadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName = {}) override;
    [[nodiscard]] PucLuaLoadResult LoadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName, std::uint64_t contentHash);
    [[nodiscard]] PucLuaLoadResult ReloadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName = {}, std::uint64_t contentHash = 0U);
    void UnloadScript(kb::assets::AssetId assetId) noexcept override;
    void Clear() noexcept override;
    [[nodiscard]] bool HasScript(kb::assets::AssetId assetId) const noexcept override;
    [[nodiscard]] bool IsScriptCurrent(kb::assets::AssetId assetId, std::uint64_t contentHash) const noexcept;

    [[nodiscard]] PucLuaLoadResult RegisterModule(std::string name, std::string source, std::string chunkName = {});
    [[nodiscard]] PucLuaLoadResult RegisterModule(std::string name, std::string source, std::string chunkName, std::uint64_t contentHash);
    void UnloadModule(std::string_view name) noexcept;
    void ClearModules() noexcept;
    [[nodiscard]] bool HasModule(std::string_view name) const noexcept;
    [[nodiscard]] bool IsModuleCurrent(std::string_view name, std::uint64_t contentHash) const noexcept;

    void SetScriptExposedVariables(
        kb::assets::AssetId assetId,
        std::span<const ScriptApiPin> variables,
        std::span<const ScriptValue> defaults,
        std::span<const std::uint8_t> hasDefaults);
    [[nodiscard]] std::span<const PucLuaExposedVariableInstance> InstanceVariables(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) const noexcept;
    [[nodiscard]] std::size_t SuspendedCoroutineCount() const noexcept;
    [[nodiscard]] bool SetInstanceVariable(kb::scene::SceneEntity entity, kb::assets::AssetId assetId, std::string_view name, ScriptValue value);
    // Editor-authored per-instance override: unlike SetInstanceVariable it
    // CREATES the (entity,asset) instance record if it does not exist yet (so it
    // can be seeded before the behaviour's first execution), types the value from
    // the asset's declared @expose definition, and marks it overridden so the
    // per-frame default re-sync preserves it.
    void SetInstanceVariableOverride(kb::scene::SceneEntity entity, kb::assets::AssetId assetId, std::string_view name, ScriptValue value) override;

    void SetDebugSettings(PucLuaDebugSettings settings);
    void SetExecutionBudgetSettings(ScriptExecutionBudgetSettings settings) noexcept;
    void BeginExecutionBudget() noexcept;
    void EndExecutionBudget() noexcept;
    [[nodiscard]] kb::core::BudgetExceededPolicy ExecutionBudgetPolicy() const noexcept;
    [[nodiscard]] bool IsExecutionBudgetEnabled() const noexcept;
    [[nodiscard]] bool HasActiveExecutionBudget() const noexcept;
    // Bytes the Lua state currently holds, counted by its bounded allocator.
    [[nodiscard]] std::size_t LuaMemoryUsedBytes() const noexcept;
    // Blocks the Lua state has asked its allocator for since it was created; two readings bracket the allocations
    // a stretch of script work made.
    [[nodiscard]] std::uint64_t LuaAllocationCount() const noexcept;
    // Allocator block capacities/headers and checkpoint image storage, not process RSS.
    [[nodiscard]] std::size_t LuaMemoryOwnedBytes() const noexcept;
    [[nodiscard]] const PucLuaDebugSettings& DebugSettings() const noexcept;
    void RequestBreakOnNextLine() noexcept;
    void RequestStepInto() noexcept;
    void ResumeDebugExecution() noexcept;
    // While a script is suspended these release it; it resumes on the next invocation of its entry and pauses
    // again at the next line of the same or a calling function (over), or of a calling function (out). Without a
    // suspended script they break on the next line, like RequestStepInto.
    void RequestStepOver() noexcept;
    void RequestStepOut() noexcept;
    // True while a script is suspended at a pause that no Continue or step has released yet.
    [[nodiscard]] bool IsDebugPaused() const noexcept;
    // The suspended script's call stack and locals as they are now; invalid when no script is suspended.
    [[nodiscard]] PucLuaDebugPauseSnapshot InspectDebugPause() const;
    // Evaluate an expression against scalar locals/upvalues in an isolated, bounded state.
    [[nodiscard]] PucLuaDebugEvaluationResult EvaluateDebugExpression(std::string_view expression, int frame = 0) const;
    // Line hook side: the requested pause that applies to `thread` on the current line, if any.
    [[nodiscard]] std::optional<PucLuaDebugPauseReason> ConsumeRequestedDebugPause(lua_State* thread) noexcept;
    // Line hook side: whether `thread` may yield from the hook to stay suspended at a pause; true marks it as
    // suspending. False while a Destroyed entry runs, which is never invoked again to resume.
    [[nodiscard]] bool BeginDebugSuspend(lua_State* thread) noexcept;
    // LuaJIT runs the hook again for the line a thread was suspended at when it is resumed; true once for that first event,
    // which is not a new line to stop at.
    [[nodiscard]] bool ConsumeResumedLine(lua_State* thread, int line) noexcept;
    void RecordDebugPause(PucLuaDebugPauseSnapshot snapshot);
    [[nodiscard]] std::optional<PucLuaDebugPauseReason> ConsumeRequestedDebugPause() noexcept;
    [[nodiscard]] bool NeedsDebugLineHook() const noexcept;
    [[nodiscard]] bool NeedsDebugHook() const noexcept;
    [[nodiscard]] const PucLuaDebugPauseSnapshot& LastDebugPause() const noexcept;
    void ClearDebugPause() noexcept;
    [[nodiscard]] bool PushModuleForImport(std::string_view name, std::string& error);
    void ResetAssetForHotReload(kb::assets::AssetId assetId, ScriptEventBus& events) noexcept override;
    void TrackEventSubscription(
        kb::scene::SceneEntity entity,
        kb::assets::AssetId assetId,
        EventSubscriptionHandle handle);

    // True unless the script of `behaviour` is loaded and defines nothing called `name`; also true while any entry is
    // suspended in a coroutine, which must be resumed whatever the script defines now.
    [[nodiscard]] bool HasEntry(
        const kb::scene::BehaviourComponent& behaviour,
        std::string_view name) override;

    // An event delivered to many behaviours reaches each of them as the same Lua table: it is built for the first
    // receiver and handed to the others, so a broadcast costs one table, not one per receiver. A handler that changes
    // the table changes it for the receivers after it.
    void BeginEventBroadcast(const ScriptEvent& event) override;
    void EndEventBroadcast() noexcept override;

    // The call a phase loop makes for an object: it keeps what the call needs (the script, the object's self table)
    // so a frame does not look them up for every object. It stays valid until PhaseCallRevision moves.
    [[nodiscard]] bool ResolvePhaseCall(
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptLifecycleEvent event,
        ScriptPhaseCall& call) override;
    // The same for the handlers of a broadcast: the name of the handler is the incoming event's.
    [[nodiscard]] bool ResolveEventCall(
        kb::scene::SceneEntity entity,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptPhaseCall& call) override;
    [[nodiscard]] std::uint64_t PhaseCallRevision() const noexcept override;

    // Runs the collector for at most ScriptExecutionBudgetSettings::luaCollectionMicrosecondsPerFrame (the first
    // call also switches the state to generational mode, where a step is one collection of the objects created since
    // the last one - the per-frame garbage of scripts). The scene system host calls it once at the end of a frame.
    void StepGarbageCollector() noexcept;
    [[nodiscard]] PucLuaCollectionStats CollectionStats() const noexcept;

    [[nodiscard]] ScriptBackendExecutionResult ExecuteLifecycle(
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context) override;
    [[nodiscard]] ScriptBackendExecutionResult ExecuteEvent(
        const kb::scene::BehaviourComponent& behaviour,
        const ScriptEvent& event,
        EventId eventId,
        ScriptExecutionContext& context) override;

private:
    friend class PucLuaEventsApi;
    friend class PucLuaRuntimeApi;
    friend class ScriptRuntimeHost;
    friend struct ScriptRuntimeHostState;
    struct RuntimeSnapshot;
    [[nodiscard]] std::unique_ptr<kb::core::RuntimeStateSnapshot> CaptureRuntimeStateImpl(std::string& error, bool coordinatedHost);
    [[nodiscard]] bool ValidateRuntimeStateImpl(const kb::core::RuntimeStateSnapshot& state, std::string& error, bool coordinatedHost) const;
    [[nodiscard]] bool RestoreRuntimeStateImpl(const kb::core::RuntimeStateSnapshot& state, std::string& error, bool coordinatedHost);

    [[nodiscard]] ScriptBackendExecutionResult ExecuteFunction(
        const kb::scene::BehaviourComponent& behaviour,
        std::string_view functionName,
        ScriptExecutionContext& context,
        const ScriptEvent* event);
    [[nodiscard]] int FindScriptEnvironment(kb::assets::AssetId assetId) const noexcept;
    // Idle coroutine threads are kept in a table in the Lua registry (so a state checkpoint restores them with the
    // rest of the heap): an entry that returns hands its thread back instead of leaving it to the collector, and the
    // next entry starts on it. AcquireThread leaves the thread on the state's stack.
    [[nodiscard]] lua_State* AcquireThread();
    void ReleaseThread(lua_State* thread, int threadIndex) noexcept;

    struct ScriptRecord {
        int environmentRef = -2;
        std::string chunkName;
        std::uint64_t contentHash = 0U;
        std::uint64_t generation = 0U;
        // The function set the environment has the execution API attached for (see PucLuaRuntimeApi::AttachExecutionApi).
        // The stamp of a context without a function registry is 0, so a new script starts below every stamp.
        long long attachedStamp = -1;
    };
    // The record of an asset; the last one asked for is remembered, because the objects of one script are called one
    // after another. Null when the asset has no script.
    [[nodiscard]] ScriptRecord* FindScriptRecord(std::uint64_t assetId) noexcept;
    ScriptRecord* cachedScript_ = nullptr;
    std::uint64_t cachedScriptAsset_ = 0U;

    // The event of each broadcast in progress and the registry reference of its table, once it was built.
    struct EventBroadcast {
        const ScriptEvent* event = nullptr;
        int tableReference = -2;
        // The event's name as an interned string anchored in the registry (LuaJIT: the receivers find their handler without
        // hashing the name again); zero when there is none.
        int nameReference = 0;
    };
    std::vector<EventBroadcast> eventBroadcasts_;

    struct ModuleRecord {
        std::string source;
        std::string chunkName;
        int valueRef = -2;
        std::uint64_t contentHash = 0U;
        std::uint64_t generation = 0U;
        bool loading = false;
    };

    struct ExposedVariableRecord {
        ScriptApiPin pin;
        ScriptValue defaultValue;
        bool hasDefault = false;
    };

    enum class DebugStepMode {
        Run,
        BreakOnNextLine,
        StepInto,
        StepOver,
        StepOut,
    };

    struct InstanceKey {
        std::uint64_t entityId = 0U;
        std::uint64_t assetId = 0U;

        [[nodiscard]] bool operator==(const InstanceKey& other) const noexcept {
            return entityId == other.entityId && assetId == other.assetId;
        }
    };

    struct InstanceKeyHasher {
        [[nodiscard]] std::size_t operator()(InstanceKey key) const noexcept {
            std::uint64_t hash = key.entityId;
            hash ^= key.assetId + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
            return static_cast<std::size_t>(hash);
        }
    };

    // The entry a debugger pause suspended; its coroutine stays in coroutineRefs_ like a yielded one.
    struct DebugPause {
        lua_State* thread = nullptr;
        InstanceKey instance{};
        std::string functionName;
        int depth = 0;
    };

    // What the calls of one object share, kept across frames: the script record, the registry reference of its self table
    // and whether it has exposed variables to bring in line. Refreshed when callSiteRevision_ moved.
    struct CallSite;
    // What a phase loop is handed for one phase of one object: the site and the name of the entry it runs.
    struct PhaseTarget {
        CallSite* site = nullptr;
        const char* name = nullptr;
        int nameReference = 0;
    };
    struct CallSite {
        PucLuaScriptRuntime* runtime = nullptr;
        InstanceKey key{};
        ScriptRecord* script = nullptr;
        int selfReference = -2;
        std::uint64_t revision = 0U;
        bool plain = false;
        // The function set the script environment has to be attached for. Once the function registry is locked it cannot
        // change, and the value is kept until the site is refreshed.
        long long apiStamp = 0;
        bool apiStampFinal = false;
        std::array<PhaseTarget, static_cast<std::size_t>(ScriptLifecycleEvent::Destroyed) + 1U> phases{};
    };
    [[nodiscard]] CallSite& SiteFor(const InstanceKey& key);
    void RefreshSite(CallSite& site);
    // Runs one entry of an object on the kept coroutine when nothing special applies (no suspended entries, no
    // debugger, an object with a self table and no exposed variables). False when the general path has to run it.
    [[nodiscard]] bool TryLeanExecute(
        CallSite& site,
        const char* functionName,
        int nameReference,
        const ScriptEvent* event,
        ScriptExecutionContext& context,
        std::vector<ScriptDiagnostic>& diagnostics,
        bool& executed);
    void FinishLeanResume(
        CallSite& site,
        lua_State* thread,
        int status,
        const char* functionName,
        ScriptExecutionContext& context,
        std::vector<ScriptDiagnostic>& diagnostics,
        bool& executed);
    // Pushes the event table of a handler on `target`: the shared one while a broadcast is in progress.
    void PushEventValue(lua_State* target, const ScriptEvent& event);
    // The registry reference of an interned entry name (zero: none is kept). A lifecycle name is kept for the life of the
    // state; an event name only while its broadcast is in progress.
    [[nodiscard]] int PhaseNameReference(ScriptLifecycleEvent event);
    [[nodiscard]] int EventNameReference(const ScriptEvent& event);
    std::array<int, static_cast<std::size_t>(ScriptLifecycleEvent::Destroyed) + 1U> phaseNameReferences_{};
    [[nodiscard]] static bool InvokePhase(
        void* target,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context,
        std::vector<ScriptDiagnostic>& diagnostics);
    [[nodiscard]] static bool InvokeEventHandler(
        void* target,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context,
        std::vector<ScriptDiagnostic>& diagnostics);
    [[nodiscard]] static bool InvokeEntry(
        CallSite& site,
        const char* name,
        int nameReference,
        const ScriptEvent* event,
        const kb::scene::BehaviourComponent& behaviour,
        ScriptExecutionContext& context,
        std::vector<ScriptDiagnostic>& diagnostics);

    void ReleaseDebugPause(DebugStepMode mode) noexcept;
    void ClearCoroutines(const InstanceKey& instanceKey) noexcept;
    void ClearCoroutinesForAsset(kb::assets::AssetId assetId) noexcept;
    void TrackEventSubscription(const InstanceKey& instanceKey, EventSubscriptionHandle handle);
    void ClearEventSubscriptions(const InstanceKey& instanceKey, ScriptEventBus& events) noexcept;
    void ClearEventSubscriptionsForAsset(kb::assets::AssetId assetId, ScriptEventBus& events) noexcept;

    // Declared before state_: the state's allocator writes into it from the
    // first allocation lua_newstate makes until lua_close frees the last one.
    std::unique_ptr<PucLuaMemoryBudget> memoryBudget_;
    std::shared_ptr<PucLuaStateLifetime> stateLifetime_;
    lua_State* state_ = nullptr;
    // Ends an entry that has run past the execution budget (see PucLuaRunaway.hpp).
    std::unique_ptr<PucLuaRunaway> runaway_;
    std::unordered_map<std::uint64_t, ScriptRecord> scripts_;
    std::unordered_map<std::string, ModuleRecord> modules_;
    std::unordered_map<std::uint64_t, std::vector<ExposedVariableRecord>> exposedVariables_;
    // The exposed variables of one behaviour instance, and the definitions revision they were last synchronized
    // with: an instance is brought in line with its asset's definitions when they change, not on every call.
    struct InstanceVariableSet {
        std::vector<PucLuaExposedVariableInstance> values;
        std::uint64_t syncedRevision = 0U;
    };
    std::unordered_map<InstanceKey, InstanceVariableSet, InstanceKeyHasher> instanceVariables_;
    std::uint64_t exposedRevision_ = 1U;
    std::unordered_map<InstanceKey, CallSite, InstanceKeyHasher> callSites_;
    // Moves when a script record or an exposed-variable definition changes, or the sites are dropped.
    std::uint64_t callSiteRevision_ = 1U;
    // LIB-097: registry references own Lua generator threads. Each thread is
    // scoped to one behaviour instance and entry function, so yielding Tick
    // never suspends another entity or lifecycle callback.
    std::unordered_map<InstanceKey, std::unordered_map<std::string, int>, InstanceKeyHasher> coroutineRefs_;
    std::unordered_map<InstanceKey, std::vector<EventSubscriptionHandle>, InstanceKeyHasher> eventSubscriptionHandles_;
    PucLuaDebugSettings debugSettings_;
    PucLuaDebugPauseSnapshot lastDebugPause_;
    DebugStepMode debugStepMode_ = DebugStepMode::Run;
    // The coroutine a step is bound to (null: the next line of any script) and its call depth when it paused.
    lua_State* debugStepThread_ = nullptr;
    int debugStepDepth_ = 0;
    std::optional<DebugPause> debugPause_;
    // Set by the line hook right before it yields a coroutine for a pause, read when lua_resume returns.
    lua_State* debugSuspendThread_ = nullptr;
    int debugSuspendDepth_ = 0;
    bool debugSuspendAllowed_ = false;
    // The thread last released from a pause and the line it paused at, until its first line event after the release.
    lua_State* debugResumedThread_ = nullptr;
    int debugResumedLine_ = 0;
    ScriptExecutionBudgetSettings executionBudgetSettings_;
    bool executionBudgetActive_ = false;
    std::uint64_t generation_ = 1U;
    PucLuaCollectionStats collectionStats_;
};

} // namespace kb::script
