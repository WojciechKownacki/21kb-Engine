#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/LuaScriptBackend.hpp"
#include "engine/script/ScriptApiNameRegistry.hpp"
#include "engine/script/ScriptEventBus.hpp"
#include "engine/script/ScriptExecutionBudget.hpp"
#include "engine/script/ScriptValue.hpp"

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

struct PucLuaMemoryBudget;

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

class PucLuaScriptRuntime final : public ILuaScriptRuntime, public ILuaScriptAssetStore {
public:
    PucLuaScriptRuntime();
    ~PucLuaScriptRuntime() override;

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
    [[nodiscard]] bool ConsumeLuaInstructions(std::size_t count) noexcept;
    [[nodiscard]] kb::core::BudgetExceededPolicy ExecutionBudgetPolicy() const noexcept;
    [[nodiscard]] bool IsExecutionBudgetEnabled() const noexcept;
    [[nodiscard]] bool HasActiveExecutionBudget() const noexcept;
    // Bytes the Lua state currently holds, counted by its bounded allocator.
    [[nodiscard]] std::size_t LuaMemoryUsedBytes() const noexcept;
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
    // Line hook side: the requested pause that applies to `thread` on the current line, if any.
    [[nodiscard]] std::optional<PucLuaDebugPauseReason> ConsumeRequestedDebugPause(lua_State* thread) noexcept;
    // Line hook side: whether `thread` may yield from the hook to stay suspended at a pause; true marks it as
    // suspending. False while a Destroyed entry runs, which is never invoked again to resume.
    [[nodiscard]] bool BeginDebugSuspend(lua_State* thread) noexcept;
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

    [[nodiscard]] ScriptBackendExecutionResult ExecuteFunction(
        const kb::scene::BehaviourComponent& behaviour,
        std::string_view functionName,
        ScriptExecutionContext& context,
        const ScriptEvent* event);
    [[nodiscard]] int FindScriptEnvironment(kb::assets::AssetId assetId) const noexcept;

    struct ScriptRecord {
        int environmentRef = -2;
        std::string chunkName;
        std::uint64_t contentHash = 0U;
        std::uint64_t generation = 0U;
    };

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

    void ReleaseDebugPause(DebugStepMode mode) noexcept;
    void ClearCoroutines(const InstanceKey& instanceKey) noexcept;
    void ClearCoroutinesForAsset(kb::assets::AssetId assetId) noexcept;
    void TrackEventSubscription(const InstanceKey& instanceKey, EventSubscriptionHandle handle);
    void ClearEventSubscriptions(const InstanceKey& instanceKey, ScriptEventBus& events) noexcept;
    void ClearEventSubscriptionsForAsset(kb::assets::AssetId assetId, ScriptEventBus& events) noexcept;

    // Declared before state_: the state's allocator writes into it from the
    // first allocation lua_newstate makes until lua_close frees the last one.
    std::unique_ptr<PucLuaMemoryBudget> memoryBudget_;
    lua_State* state_ = nullptr;
    std::unordered_map<std::uint64_t, ScriptRecord> scripts_;
    std::unordered_map<std::string, ModuleRecord> modules_;
    std::unordered_map<std::uint64_t, std::vector<ExposedVariableRecord>> exposedVariables_;
    std::unordered_map<InstanceKey, std::vector<PucLuaExposedVariableInstance>, InstanceKeyHasher> instanceVariables_;
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
    ScriptExecutionBudgetSettings executionBudgetSettings_;
    std::size_t remainingLuaInstructions_ = 0U;
    bool executionBudgetActive_ = false;
    std::uint64_t generation_ = 1U;
};

} // namespace kb::script
