#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/script/LuaScriptBackend.hpp"
#include "engine/script/ScriptApiNameRegistry.hpp"
#include "engine/script/ScriptValue.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace kb::script {

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
    [[nodiscard]] const PucLuaDebugSettings& DebugSettings() const noexcept;
    void RequestBreakOnNextLine() noexcept;
    void RequestStepInto() noexcept;
    void ResumeDebugExecution() noexcept;
    void RecordDebugPause(PucLuaDebugPauseSnapshot snapshot);
    [[nodiscard]] std::optional<PucLuaDebugPauseReason> ConsumeRequestedDebugPause() noexcept;
    [[nodiscard]] bool NeedsDebugHook() const noexcept;
    [[nodiscard]] const PucLuaDebugPauseSnapshot& LastDebugPause() const noexcept;
    void ClearDebugPause() noexcept;
    [[nodiscard]] bool PushModuleForImport(std::string_view name, std::string& error);

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

    void ClearCoroutines(const InstanceKey& instanceKey) noexcept;
    void ClearCoroutinesForAsset(kb::assets::AssetId assetId) noexcept;

    lua_State* state_ = nullptr;
    std::unordered_map<std::uint64_t, ScriptRecord> scripts_;
    std::unordered_map<std::string, ModuleRecord> modules_;
    std::unordered_map<std::uint64_t, std::vector<ExposedVariableRecord>> exposedVariables_;
    std::unordered_map<InstanceKey, std::vector<PucLuaExposedVariableInstance>, InstanceKeyHasher> instanceVariables_;
    // LIB-097: registry references own Lua generator threads. Each thread is
    // scoped to one behaviour instance and entry function, so yielding Tick
    // never suspends another entity or lifecycle callback.
    std::unordered_map<InstanceKey, std::unordered_map<std::string, int>, InstanceKeyHasher> coroutineRefs_;
    PucLuaDebugSettings debugSettings_;
    PucLuaDebugPauseSnapshot lastDebugPause_;
    DebugStepMode debugStepMode_ = DebugStepMode::Run;
    std::uint64_t generation_ = 1U;
};

} // namespace kb::script
