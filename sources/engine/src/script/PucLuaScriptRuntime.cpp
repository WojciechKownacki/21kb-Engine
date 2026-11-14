#include "script/lua/LuaVm.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"

#include "script/lua/PucLuaDebugHook.hpp"
#include "script/lua/PucLuaRunaway.hpp"
#include "script/lua/PucLuaRuntimeApi.hpp"
#include "script/lua/PucLuaSandboxEnvironment.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"
#include "script/lua/PucLuaValueBridge.hpp"
#include "script/lua/PucLuaMemoryArena.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

constexpr int kNoReference = LUA_NOREF;

// Registry key of the table of idle coroutine threads, and how many it keeps.
constexpr char kThreadPoolKey = 0;
constexpr std::size_t kMaxIdleThreads = 8U;

// Number of active call frames of a Lua thread.
[[nodiscard]] int CallDepth(lua_State* thread) noexcept {
    lua_Debug frame{};
    int depth = 0;
    while (lua_getstack(thread, depth, &frame) != 0) {
        ++depth;
    }
    return depth;
}

// A coroutine the line or count hook suspended stopped inside a Lua function, not inside coroutine.yield. Lua
// resumes it by running that function on and discards whatever lua_resume passes, so it gets no arguments and
// its stack must not be touched.
[[nodiscard]] bool SuspendedInsideHook(lua_State* thread) noexcept {
    lua_Debug frame{};
    if (lua_getstack(thread, 0, &frame) == 0 || lua_getinfo(thread, "S", &frame) == 0) {
        return false;
    }
    return frame.what != nullptr && std::string_view{ frame.what } != "C";
}

// Drops the copy of the innermost frame's locals made at a debugger pause (see PucLuaStateLifetime::pausedFrameLocals).
void ForgetPausedFrameLocals(lua_State* state, PucLuaStateLifetime& lifetime) noexcept {
    if (state != nullptr && lifetime.pausedFrameLocals != LUA_NOREF) luaL_unref(state, LUA_REGISTRYINDEX, lifetime.pausedFrameLocals);
    lifetime.pausedFrameLocals = LUA_NOREF;
}

// The message luaL_newstate's panic handler prints before Lua aborts.
int Panic(lua_State* state) {
    const char* message = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : "error object is not a string";
    std::fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n", message);
    std::fflush(stderr);
    return 0;
}

} // namespace

PucLuaScriptRuntime::PucLuaScriptRuntime()
    : memoryBudget_(std::make_unique<PucLuaMemoryBudget>(PucLuaMemoryBudget{ .limitBytes = ScriptExecutionBudgetSettings{}.luaMemoryBytes }))
    , stateLifetime_(std::make_shared<PucLuaStateLifetime>())
    , state_(lua_newstate(&PucLuaMemoryBudget::Allocate, memoryBudget_.get())) {
    stateLifetime_->state = state_;
    if (state_ != nullptr) {
        lua_atpanic(state_, &Panic);
        memoryBudget_->owner = this;
        runaway_ = PucLuaRunaway::Create(state_, *this);
        PucLuaSandboxEnvironment::OpenSafeLibraries(state_);
#if !defined(KB_SCRIPT_VM_LUAJIT)
        // Scripts make a stream of short-lived tables (self, events, results); the generational collector reclaims
        // those without marking the long-lived heap again.
        static_cast<void>(lua_gc(state_, LUA_GCGEN, 0, 0));
#endif
    }
}

PucLuaScriptRuntime::~PucLuaScriptRuntime() {
    stateLifetime_->state = nullptr;
    runaway_.reset();
    if (state_ != nullptr) {
        lua_close(state_);
        state_ = nullptr;
    }
}

PucLuaLoadResult PucLuaScriptRuntime::LoadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName) {
    return LoadScript(assetId, source, chunkName, 0U);
}

PucLuaLoadResult PucLuaScriptRuntime::LoadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName, std::uint64_t contentHash) {
    return ReloadScript(assetId, source, chunkName, contentHash);
}

PucLuaLoadResult PucLuaScriptRuntime::ReloadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName, std::uint64_t contentHash) {
    if (state_ == nullptr) {
        return PucLuaLoadResult{ .error = "lua state could not be created" };
    }
    if (!assetId.IsValid()) {
        return PucLuaLoadResult{ .error = "lua script asset id is invalid" };
    }

    PucLuaStackGuard stack{ state_ };
    PucLuaSandboxEnvironment::Create(state_);
    const int environmentIndex = lua_gettop(state_);
    if (std::optional<std::string> error = PucLuaRuntimeApi::AttachRuntimeFunctions(state_, environmentIndex, *this)) {
        return PucLuaLoadResult{ .error = std::move(*error) };
    }

    // "@name" makes Lua report errors as "name:line:" with the whole path; a bare name would be reported as
    // [string "name"]:line: and cut to a few dozen characters.
    const std::string chunk = chunkName.empty() ? std::string{ "@lua-script-" } + std::to_string(assetId.value)
        : (chunkName.front() == '@' || chunkName.front() == '=' ? std::string{ chunkName } : "@" + std::string{ chunkName });
    if (luaL_loadbufferx(state_, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK) {
        return PucLuaLoadResult{ .error = PucLuaErrorReporter::ErrorFromTop(state_) };
    }

    lua_pushvalue(state_, environmentIndex);
    kb_lua_set_chunk_environment(state_);
    lua_pushcfunction(state_, &PucLuaErrorReporter::Traceback);
    const int errorHandlerIndex = lua_gettop(state_) - 1;
    lua_insert(state_, errorHandlerIndex);
    // The code at the top of a script runs under the same instruction budget as its functions: a loop there would
    // otherwise hang the host while the script loads or reloads.
    const bool budgetArmedHere = !executionBudgetActive_;
    if (budgetArmedHere) BeginExecutionBudget();
    PucLuaDebugHook::Install(state_, *this);
    const int status = [&] {
        const PucLuaMemoryLimitScope memoryLimit{ state_ };
        return lua_pcall(state_, 0, 0, errorHandlerIndex);
    }();
    PucLuaDebugHook::Clear(state_);
    if (budgetArmedHere) EndExecutionBudget();
    if (status != LUA_OK) {
        return PucLuaLoadResult{ .error = PucLuaErrorReporter::ErrorFromTop(state_) };
    }
    lua_remove(state_, errorHandlerIndex);

    lua_pushvalue(state_, environmentIndex);
    const int environmentRef = luaL_ref(state_, LUA_REGISTRYINDEX);
    const auto old = scripts_.find(assetId.value);
    if (old != scripts_.end()) {
        ClearCoroutinesForAsset(assetId);
        luaL_unref(state_, LUA_REGISTRYINDEX, old->second.environmentRef);
    }
    scripts_[assetId.value] = ScriptRecord{
        .environmentRef = environmentRef,
        .chunkName = chunk,
        .contentHash = contentHash,
        .generation = generation_++,
    };
    ++callSiteRevision_;
    return PucLuaLoadResult{ .succeeded = true };
}

void PucLuaScriptRuntime::UnloadScript(kb::assets::AssetId assetId) noexcept {
    if (state_ == nullptr) {
        return;
    }
    const auto iter = scripts_.find(assetId.value);
    if (iter == scripts_.end()) {
        return;
    }
    luaL_unref(state_, LUA_REGISTRYINDEX, iter->second.environmentRef);
    ClearCoroutinesForAsset(assetId);
    std::erase_if(stateLifetime_->selfReferences, [&](const auto& entry) {
        if (entry.first.assetId != assetId.value) return false;
        luaL_unref(state_, LUA_REGISTRYINDEX, entry.second);
        return true;
    });
    cachedScript_ = nullptr;
    ++callSiteRevision_;
    scripts_.erase(iter);
    exposedVariables_.erase(assetId.value);
    std::erase_if(instanceVariables_, [assetId](const auto& entry) {
        return entry.first.assetId == assetId.value;
    });
}

void PucLuaScriptRuntime::Clear() noexcept {
    if (state_ != nullptr) {
        for (const auto& [assetId, record] : scripts_) {
            static_cast<void>(assetId);
            luaL_unref(state_, LUA_REGISTRYINDEX, record.environmentRef);
        }
        for (const auto& [name, record] : modules_) {
            static_cast<void>(name);
            if (record.valueRef != kNoReference) {
                luaL_unref(state_, LUA_REGISTRYINDEX, record.valueRef);
            }
        }
        for (const auto& [instance, functions] : coroutineRefs_) {
            static_cast<void>(instance);
            for (const auto& [name, reference] : functions) {
                static_cast<void>(name);
                luaL_unref(state_, LUA_REGISTRYINDEX, reference);
            }
        }
        for (const auto& [object, reference] : stateLifetime_->selfReferences) {
            static_cast<void>(object);
            luaL_unref(state_, LUA_REGISTRYINDEX, reference);
        }
        if (stateLifetime_->spareThread != nullptr) {
            luaL_unref(state_, LUA_REGISTRYINDEX, stateLifetime_->spareThreadReference);
        }
    }
    if (state_ != nullptr) {
        for (const int reference : phaseNameReferences_) {
            if (reference != 0) luaL_unref(state_, LUA_REGISTRYINDEX, reference);
        }
    }
    phaseNameReferences_.fill(0);
    stateLifetime_->selfReferences.clear();
    stateLifetime_->spareThread = nullptr;
    stateLifetime_->spareThreadReference = -2;
    stateLifetime_->spareThreadBusy = false;
    cachedScript_ = nullptr;
    callSites_.clear();
    ++callSiteRevision_;
    ForgetPausedFrameLocals(state_, *stateLifetime_);
    scripts_.clear();
    modules_.clear();
    exposedVariables_.clear();
    instanceVariables_.clear();
    coroutineRefs_.clear();
    eventSubscriptionHandles_.clear();
    lastDebugPause_ = {};
    debugStepMode_ = DebugStepMode::Run;
    debugStepThread_ = nullptr;
    debugPause_.reset();
    debugSuspendThread_ = nullptr;
}

bool PucLuaScriptRuntime::HasScript(kb::assets::AssetId assetId) const noexcept {
    return scripts_.contains(assetId.value);
}

bool PucLuaScriptRuntime::IsScriptCurrent(kb::assets::AssetId assetId, std::uint64_t contentHash) const noexcept {
    const auto iter = scripts_.find(assetId.value);
    return iter != scripts_.end() && (contentHash == 0U || iter->second.contentHash == contentHash);
}

std::size_t PucLuaScriptRuntime::SuspendedCoroutineCount() const noexcept {
    std::size_t count = 0U;
    for (const auto& [instance, functions] : coroutineRefs_) {
        static_cast<void>(instance);
        count += functions.size();
    }
    return count;
}

PucLuaLoadResult PucLuaScriptRuntime::RegisterModule(std::string name, std::string source, std::string chunkName) {
    return RegisterModule(std::move(name), std::move(source), std::move(chunkName), 0U);
}

PucLuaLoadResult PucLuaScriptRuntime::RegisterModule(std::string name, std::string source, std::string chunkName, std::uint64_t contentHash) {
    if (state_ == nullptr) {
        return PucLuaLoadResult{ .error = "lua state could not be created" };
    }
    if (name.empty()) {
        return PucLuaLoadResult{ .error = "lua module name is empty" };
    }
    const auto existing = modules_.find(name);
    if (existing != modules_.end() && existing->second.loading) {
        return {.error = "lua module cannot be replaced while it is loading: " + name};
    }
    std::optional<ModuleRecord> previous;
    if (existing != modules_.end()) previous = std::move(existing->second);
    if (chunkName.empty()) chunkName = name;
    if (chunkName.front() != '@' && chunkName.front() != '=') chunkName.insert(chunkName.begin(), '@');
    modules_[name] = ModuleRecord{
        .source = std::move(source),
        .chunkName = std::move(chunkName),
        .valueRef = kNoReference,
        .contentHash = contentHash,
        .generation = generation_++,
    };
    if (previous) {
        // Initial registration stays lazy so dependencies can be registered in
        // either order. A replacement must work before releasing its old value.
        PucLuaStackGuard stack{state_};
        const bool budgetArmedHere = !executionBudgetActive_;
        if (budgetArmedHere) BeginExecutionBudget();
        std::string error;
        const bool succeeded = PushModuleForImport(name, error);
        if (budgetArmedHere) EndExecutionBudget();
        if (!succeeded) {
            UnloadModule(name);
            modules_[name] = std::move(*previous);
            return {.error = std::move(error)};
        }
        if (previous->valueRef != kNoReference)
            luaL_unref(state_, LUA_REGISTRYINDEX, previous->valueRef);
    }
    return PucLuaLoadResult{ .succeeded = true };
}

void PucLuaScriptRuntime::UnloadModule(std::string_view name) noexcept {
    if (state_ == nullptr) {
        return;
    }
    const auto iter = modules_.find(std::string{ name });
    if (iter == modules_.end()) {
        return;
    }
    if (iter->second.valueRef != kNoReference) {
        luaL_unref(state_, LUA_REGISTRYINDEX, iter->second.valueRef);
    }
    modules_.erase(iter);
}

void PucLuaScriptRuntime::ClearModules() noexcept {
    if (state_ != nullptr) {
        for (const auto& [name, module] : modules_) {
            static_cast<void>(name);
            if (module.valueRef != kNoReference) {
                luaL_unref(state_, LUA_REGISTRYINDEX, module.valueRef);
            }
        }
    }
    modules_.clear();
}

bool PucLuaScriptRuntime::HasModule(std::string_view name) const noexcept {
    return modules_.contains(std::string{ name });
}

bool PucLuaScriptRuntime::IsModuleCurrent(std::string_view name, std::uint64_t contentHash) const noexcept {
    const auto iter = modules_.find(std::string{ name });
    return iter != modules_.end() && (contentHash == 0U || iter->second.contentHash == contentHash);
}

void PucLuaScriptRuntime::SetScriptExposedVariables(
    kb::assets::AssetId assetId,
    std::span<const ScriptApiPin> variables,
    std::span<const ScriptValue> defaults,
    std::span<const std::uint8_t> hasDefaults) {
    if (!assetId.IsValid()) {
        return;
    }
    std::vector<ExposedVariableRecord> records;
    records.reserve(variables.size());
    for (std::size_t index = 0U; index < variables.size(); ++index) {
        if (variables[index].name.empty() || variables[index].type == ScriptValueType::Void) {
            continue;
        }
        const ScriptValue defaultValue = index < defaults.size() ? defaults[index] : PucLuaValueBridge::DefaultFor(variables[index].type);
        records.push_back(ExposedVariableRecord{
            .pin = variables[index],
            .defaultValue = defaultValue.Type() == ScriptValueType::Void ? PucLuaValueBridge::DefaultFor(variables[index].type) : defaultValue,
            .hasDefault = index < hasDefaults.size() && hasDefaults[index] != 0U,
        });
    }
    exposedVariables_[assetId.value] = std::move(records);
    ++exposedRevision_;
    ++callSiteRevision_;
}

std::span<const PucLuaExposedVariableInstance> PucLuaScriptRuntime::InstanceVariables(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) const noexcept {
    const auto iter = instanceVariables_.find(InstanceKey{
        .entityId = entity.Id(),
        .assetId = assetId.value,
    });
    if (iter == instanceVariables_.end()) {
        return {};
    }
    return iter->second.values;
}

bool PucLuaScriptRuntime::SetInstanceVariable(kb::scene::SceneEntity entity, kb::assets::AssetId assetId, std::string_view name, ScriptValue value) {
    const auto iter = instanceVariables_.find(InstanceKey{
        .entityId = entity.Id(),
        .assetId = assetId.value,
    });
    if (iter == instanceVariables_.end()) {
        return false;
    }
    PucLuaExposedVariableInstance* variable = PucLuaValueBridge::FindVariable(iter->second.values, name);
    if (variable == nullptr || !PucLuaValueBridge::IsCompatible(value, variable->type)) {
        return false;
    }
    variable->value = PucLuaValueBridge::Coerce(std::move(value), variable->type);
    variable->overridden = true;
    return true;
}

void PucLuaScriptRuntime::SetInstanceVariableOverride(kb::scene::SceneEntity entity, kb::assets::AssetId assetId, std::string_view name, ScriptValue value) {
    if (!entity.IsValid() || !assetId.IsValid() || name.empty()) {
        return;
    }
    // Type the override from the asset's declared @expose definition when known
    // (PrepareLuaAsset runs before the seeding point, so it usually is), so it
    // coerces to exactly the type the script observes; else fall back to the
    // supplied value's own type.
    ScriptValueType declaredType = ScriptValueType::Void;
    if (const auto definitions = exposedVariables_.find(assetId.value); definitions != exposedVariables_.end()) {
        for (const ExposedVariableRecord& definition : definitions->second) {
            if (definition.pin.name == name) {
                declaredType = definition.pin.type;
                break;
            }
        }
    }
    if (declaredType == ScriptValueType::Void) {
        declaredType = value.Type();
    }
    if (declaredType == ScriptValueType::Void || !PucLuaValueBridge::IsCompatible(value, declaredType)) {
        return;
    }
    std::vector<PucLuaExposedVariableInstance>& variables = instanceVariables_[InstanceKey{
        .entityId = entity.Id(),
        .assetId = assetId.value,
    }].values;
    PucLuaExposedVariableInstance* variable = PucLuaValueBridge::FindVariable(variables, name);
    if (variable == nullptr) {
        variables.push_back(PucLuaExposedVariableInstance{
            .name = std::string{ name },
            .type = declaredType,
            .value = PucLuaValueBridge::Coerce(std::move(value), declaredType),
            .overridden = true,
        });
        return;
    }
    variable->type = declaredType;
    variable->value = PucLuaValueBridge::Coerce(std::move(value), declaredType);
    variable->overridden = true;
}

void PucLuaScriptRuntime::SetDebugSettings(PucLuaDebugSettings settings) {
    debugSettings_ = std::move(settings);
}

void PucLuaScriptRuntime::SetExecutionBudgetSettings(ScriptExecutionBudgetSettings settings) noexcept {
    executionBudgetSettings_ = settings;
    memoryBudget_->limitBytes = settings.luaMemoryBytes;
}

void PucLuaScriptRuntime::BeginExecutionBudget() noexcept {
    if (runaway_) runaway_->Arm(executionBudgetSettings_.luaInstructionsPerBehaviour);
    executionBudgetActive_ = true;
}

void PucLuaScriptRuntime::EndExecutionBudget() noexcept {
    if (runaway_) runaway_->Disarm();
    executionBudgetActive_ = false;
}

kb::core::BudgetExceededPolicy PucLuaScriptRuntime::ExecutionBudgetPolicy() const noexcept {
    return executionBudgetSettings_.policy;
}

bool PucLuaScriptRuntime::IsExecutionBudgetEnabled() const noexcept {
    return executionBudgetSettings_.luaInstructionsPerBehaviour != 0U;
}

std::size_t PucLuaScriptRuntime::LuaMemoryUsedBytes() const noexcept {
    return memoryBudget_->usedBytes;
}

std::uint64_t PucLuaScriptRuntime::LuaAllocationCount() const noexcept {
    return memoryBudget_->allocationCount;
}

bool PucLuaScriptRuntime::HasActiveExecutionBudget() const noexcept {
    return executionBudgetActive_ && IsExecutionBudgetEnabled();
}

const PucLuaDebugSettings& PucLuaScriptRuntime::DebugSettings() const noexcept {
    return debugSettings_;
}

void PucLuaScriptRuntime::RequestBreakOnNextLine() noexcept {
    debugStepMode_ = DebugStepMode::BreakOnNextLine;
    debugStepThread_ = nullptr;
}

void PucLuaScriptRuntime::RequestStepInto() noexcept {
    ReleaseDebugPause(DebugStepMode::StepInto);
}

void PucLuaScriptRuntime::RequestStepOver() noexcept {
    ReleaseDebugPause(debugPause_.has_value() ? DebugStepMode::StepOver : DebugStepMode::StepInto);
}

void PucLuaScriptRuntime::RequestStepOut() noexcept {
    ReleaseDebugPause(debugPause_.has_value() ? DebugStepMode::StepOut : DebugStepMode::StepInto);
}

void PucLuaScriptRuntime::ResumeDebugExecution() noexcept {
    ReleaseDebugPause(DebugStepMode::Run);
}

// A step from a pause stays on the suspended coroutine; other scripts keep running without stopping on it.
void PucLuaScriptRuntime::ReleaseDebugPause(DebugStepMode mode) noexcept {
    debugStepMode_ = mode;
    debugStepThread_ = nullptr;
    debugStepDepth_ = 0;
    if (debugPause_.has_value()) {
        ForgetPausedFrameLocals(state_, *stateLifetime_);
        debugResumedThread_ = debugPause_->thread;
        debugResumedLine_ = lastDebugPause_.line;
        if (mode != DebugStepMode::Run) {
            debugStepThread_ = debugPause_->thread;
            debugStepDepth_ = debugPause_->depth;
        }
        debugPause_.reset();
    }
}

bool PucLuaScriptRuntime::IsDebugPaused() const noexcept {
    return debugPause_.has_value();
}

PucLuaDebugPauseSnapshot PucLuaScriptRuntime::InspectDebugPause() const {
    PucLuaDebugPauseSnapshot snapshot;
    if (!debugPause_.has_value()) {
        return snapshot;
    }
    snapshot = lastDebugPause_;
#if defined(KB_SCRIPT_VM_LUAJIT)
    // A thread LuaJIT suspended in a hook reports the position of the instruction before the one it stopped at (the line,
    // and the locals the statement before it declared are missing). The stack recorded when the hook paused it, which is
    // the stack as it still is, has them right.
    if (!lastDebugPause_.callStack.empty() && debugSettings_.collectLocals) return snapshot;
#endif
    snapshot.callStack = PucLuaDebugHook::CaptureCallStack(debugPause_->thread, true);
    return snapshot;
}

PucLuaDebugEvaluationResult PucLuaScriptRuntime::EvaluateDebugExpression(std::string_view expression, int frame) const {
    if (!debugPause_) return {.error = "debugger is not paused"};
    return PucLuaDebugHook::Evaluate(debugPause_->thread, expression, frame);
}

void PucLuaScriptRuntime::RecordDebugPause(PucLuaDebugPauseSnapshot snapshot) {
    lastDebugPause_ = std::move(snapshot);
}

std::optional<PucLuaDebugPauseReason> PucLuaScriptRuntime::ConsumeRequestedDebugPause() noexcept {
    switch (debugStepMode_) {
    case DebugStepMode::BreakOnNextLine:
        debugStepMode_ = DebugStepMode::Run;
        return PucLuaDebugPauseReason::ManualBreak;
    case DebugStepMode::StepInto:
        debugStepMode_ = DebugStepMode::Run;
        debugStepThread_ = nullptr;
        return PucLuaDebugPauseReason::Step;
    case DebugStepMode::StepOver:
    case DebugStepMode::StepOut:
    case DebugStepMode::Run:
        break;
    }
    return std::nullopt;
}

std::optional<PucLuaDebugPauseReason> PucLuaScriptRuntime::ConsumeRequestedDebugPause(lua_State* thread) noexcept {
    switch (debugStepMode_) {
    case DebugStepMode::BreakOnNextLine:
        debugStepMode_ = DebugStepMode::Run;
        return PucLuaDebugPauseReason::ManualBreak;
    case DebugStepMode::StepInto:
        if (debugStepThread_ != nullptr && debugStepThread_ != thread) {
            return std::nullopt;
        }
        break;
    case DebugStepMode::StepOver:
    case DebugStepMode::StepOut: {
        if (debugStepThread_ != thread) {
            return std::nullopt;
        }
        const int depth = CallDepth(thread);
        if (debugStepMode_ == DebugStepMode::StepOver ? depth > debugStepDepth_ : depth >= debugStepDepth_) {
            return std::nullopt;
        }
        break;
    }
    case DebugStepMode::Run:
        return std::nullopt;
    }
    debugStepMode_ = DebugStepMode::Run;
    debugStepThread_ = nullptr;
    return PucLuaDebugPauseReason::Step;
}

bool PucLuaScriptRuntime::ConsumeResumedLine(lua_State* thread, int line) noexcept {
    if (debugResumedThread_ != thread) return false;
    debugResumedThread_ = nullptr;
    return line == debugResumedLine_;
}

bool PucLuaScriptRuntime::BeginDebugSuspend(lua_State* thread) noexcept {
    if (!debugSuspendAllowed_) {
        return false;
    }
    debugSuspendThread_ = thread;
    debugSuspendDepth_ = CallDepth(thread);
    return true;
}

bool PucLuaScriptRuntime::NeedsDebugLineHook() const noexcept {
    return debugStepMode_ != DebugStepMode::Run ||
           (debugSettings_.enableBreakpoints && !debugSettings_.breakpoints.empty());
}

bool PucLuaScriptRuntime::NeedsDebugHook() const noexcept {
    return NeedsDebugLineHook() || HasActiveExecutionBudget();
}

const PucLuaDebugPauseSnapshot& PucLuaScriptRuntime::LastDebugPause() const noexcept {
    return lastDebugPause_;
}

void PucLuaScriptRuntime::ClearDebugPause() noexcept {
    ForgetPausedFrameLocals(state_, *stateLifetime_);
    lastDebugPause_ = {};
}

bool PucLuaScriptRuntime::PushModuleForImport(std::string_view name, std::string& error) {
    const auto iter = modules_.find(std::string{ name });
    if (iter == modules_.end()) {
        error = "lua module is not registered: " + std::string{ name };
        return false;
    }
    ModuleRecord& module = iter->second;
    if (module.valueRef != kNoReference) {
        lua_rawgeti(state_, LUA_REGISTRYINDEX, module.valueRef);
        return true;
    }
    if (module.loading) {
        error = "lua circular module import detected: " + std::string{ name };
        return false;
    }

    const int originalTop = lua_gettop(state_);
    module.loading = true;
    PucLuaSandboxEnvironment::Create(state_);
    const int environmentIndex = lua_gettop(state_);
    if (std::optional<std::string> attachError = PucLuaRuntimeApi::AttachRuntimeFunctions(state_, environmentIndex, *this)) {
        module.loading = false;
        error = std::string{ "module runtime API initialization failed: " } + *attachError;
        lua_settop(state_, originalTop);
        return false;
    }
    if (luaL_loadbufferx(state_, module.source.data(), module.source.size(), module.chunkName.c_str(), "t") != LUA_OK) {
        error = PucLuaErrorReporter::ErrorFromTop(state_);
        module.loading = false;
        lua_settop(state_, originalTop);
        return false;
    }
    lua_pushvalue(state_, environmentIndex);
    kb_lua_set_chunk_environment(state_);
    lua_pushcfunction(state_, &PucLuaErrorReporter::Traceback);
    const int errorHandlerIndex = lua_gettop(state_) - 1;
    lua_insert(state_, errorHandlerIndex);
    const lua_Hook previousHook = lua_gethook(state_);
    const int previousMask = lua_gethookmask(state_);
    const int previousCount = lua_gethookcount(state_);
    PucLuaDebugHook::Install(state_, *this);
    const int status = [&] {
        const PucLuaMemoryLimitScope memoryLimit{ state_ };
        return lua_pcall(state_, 0, 1, errorHandlerIndex);
    }();
    // Import can run inside another module or a script's top-level code. Keep
    // that caller's debugger hook active after returning. The execution budget
    // is not part of the hook: an exhausted budget stops the importing script
    // again at its next loop or call, so protected calls cannot swallow it.
    lua_sethook(state_, previousHook, previousMask, previousCount);
    PucLuaRunaway::Rearm(state_);
    if (status != LUA_OK) {
        error = PucLuaErrorReporter::ErrorFromTop(state_);
        module.loading = false;
        lua_settop(state_, originalTop);
        return false;
    }
    if (lua_isnil(state_, -1) != 0) {
        lua_pop(state_, 1);
        lua_pushvalue(state_, environmentIndex);
    }
    module.valueRef = luaL_ref(state_, LUA_REGISTRYINDEX);
    module.loading = false;
    lua_settop(state_, originalTop);
    lua_rawgeti(state_, LUA_REGISTRYINDEX, module.valueRef);
    return true;
}

ScriptBackendExecutionResult PucLuaScriptRuntime::ExecuteLifecycle(const kb::scene::BehaviourComponent& behaviour, ScriptExecutionContext& context) {
    return ExecuteFunction(behaviour, ToString(context.Lifecycle()), context, nullptr);
}

ScriptBackendExecutionResult PucLuaScriptRuntime::ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, EventId /*eventId*/, ScriptExecutionContext& context) {
    // LIB-104: Lua resolves the event handler via lua_getglobal(event.name)
    // — inherently string-keyed by the Lua VM itself, so the typed EventId
    // has no lookup to replace here (see LuaScriptBackend.hpp's comment).
    return ExecuteFunction(behaviour, event.name, context, &event);
}

PucLuaScriptRuntime::CallSite& PucLuaScriptRuntime::SiteFor(const InstanceKey& key) {
    const auto [site, inserted] = callSites_.try_emplace(key);
    if (inserted) {
        site->second.runtime = this;
        site->second.key = key;
    }
    return site->second;
}

void PucLuaScriptRuntime::RefreshSite(CallSite& site) {
    site.script = FindScriptRecord(site.key.assetId);
    const auto self = stateLifetime_->selfReferences.find(PucLuaSelfKey{ site.key.entityId, site.key.assetId });
    site.selfReference = self == stateLifetime_->selfReferences.end() ? kNoReference : self->second;
    site.plain = (exposedVariables_.empty() || !exposedVariables_.contains(site.key.assetId)) &&
        (instanceVariables_.empty() || !instanceVariables_.contains(site.key));
    site.apiStampFinal = false;
    site.revision = callSiteRevision_;
}

void PucLuaScriptRuntime::PushEventValue(lua_State* target, const ScriptEvent& event) {
    if (eventBroadcasts_.empty() || eventBroadcasts_.back().event != &event) {
        PucLuaRuntimeApi::PushEvent(target, event);
        return;
    }
    EventBroadcast& broadcast = eventBroadcasts_.back();
    if (broadcast.tableReference == kNoReference) {
        PucLuaRuntimeApi::PushEvent(target, event);
        lua_pushvalue(target, -1);
        broadcast.tableReference = luaL_ref(target, LUA_REGISTRYINDEX);
    } else {
        lua_rawgeti(target, LUA_REGISTRYINDEX, broadcast.tableReference);
    }
}

bool PucLuaScriptRuntime::TryLeanExecute(
    CallSite& site,
    const char* functionName,
    int nameReference,
    const ScriptEvent* event,
    ScriptExecutionContext& context,
    std::vector<ScriptDiagnostic>& diagnostics,
    bool& executed) {
    PucLuaStateLifetime& lifetime = *stateLifetime_;
    if (lifetime.spareThread == nullptr || lifetime.spareThreadBusy || !coroutineRefs_.empty() || debugPause_.has_value() ||
        NeedsDebugLineHook()) {
        return false;
    }
    if (site.revision != callSiteRevision_) {
        RefreshSite(site);
    }
    if (site.script == nullptr || !site.plain || site.selfReference == kNoReference) {
        return false;
    }
    if (!site.apiStampFinal) {
        site.apiStamp = PucLuaRuntimeApi::ExecutionApiStamp(context);
        const ScriptFunctionRegistry* functions = context.Functions();
        site.apiStampFinal = functions != nullptr && functions->IsLocked();
    }
    if (site.script->attachedStamp != site.apiStamp) {
        return false;
    }

    // The entry runs on the kept coroutine, which is anchored in the registry: its stack holds the environment, the
    // function, the self table and the argument, and nothing is left on the state's own stack.
    lua_State* const thread = lifetime.spareThread;
    lua_rawgeti(thread, LUA_REGISTRYINDEX, site.script->environmentRef);
    if (nameReference != 0) {
        lua_rawgeti(thread, LUA_REGISTRYINDEX, nameReference);
        lua_rawget(thread, -2);
    } else {
        lua_getfield(thread, -1, functionName);
    }
    if (lua_isfunction(thread, -1) == 0) {
        const bool missing = lua_isnil(thread, -1) != 0;
        lua_settop(thread, 0);
        executed = false;
        return missing;
    }
    lua_rawgeti(thread, LUA_REGISTRYINDEX, site.selfReference);
    if (event == nullptr) {
        lua_pushnumber(thread, static_cast<lua_Number>(context.DeltaSeconds()));
    } else {
        PushEventValue(thread, *event);
    }
    lifetime.spareThreadBusy = true;
    const bool executionBudgetEnabled = IsExecutionBudgetEnabled();
    if (executionBudgetEnabled) {
        BeginExecutionBudget();
    }
    debugSuspendAllowed_ = true;
    int resultCount = 0;
    // The memory limit applies to the entry as it does in PucLuaMemoryLimitScope; the state's own budget is at hand.
    PucLuaMemoryBudget& memory = *memoryBudget_;
    if (memory.protectedDepth++ == 0U) {
        memory.limitReached = false;
    }
    const int status = lua_resume(thread, state_, 2, &resultCount);
    --memory.protectedDepth;
    debugSuspendAllowed_ = false;
    if (executionBudgetEnabled) {
        EndExecutionBudget();
    }
    if (status == LUA_OK) {
        lua_settop(thread, 0);
        lifetime.spareThreadBusy = false;
        executed = true;
        return true;
    }
    FinishLeanResume(site, thread, status, functionName, context, diagnostics, executed);
    return true;
}

void PucLuaScriptRuntime::FinishLeanResume(
    CallSite& site,
    lua_State* thread,
    int status,
    const char* functionName,
    ScriptExecutionContext& context,
    std::vector<ScriptDiagnostic>& diagnostics,
    bool& executed) {
    PucLuaStateLifetime& lifetime = *stateLifetime_;
    std::string coroutineError = status == LUA_YIELD ? std::string{} : PucLuaErrorReporter::ErrorWithTracebackFromTop(thread);
    if (status == LUA_YIELD) {
        // The entry is suspended: its thread stays reachable through the suspended entries of the object.
        lua_pushthread(thread);
        lua_xmove(thread, state_, 1);
        coroutineRefs_[site.key][std::string{ functionName }] = luaL_ref(state_, LUA_REGISTRYINDEX);
    }
    // A thread that yielded stays anchored by its own reference; one that failed is not run again.
    luaL_unref(state_, LUA_REGISTRYINDEX, lifetime.spareThreadReference);
    lifetime.spareThread = nullptr;
    lifetime.spareThreadReference = kNoReference;
    lifetime.spareThreadBusy = false;
    if (status == LUA_YIELD) {
        executed = true;
        return;
    }
    if (coroutineError.find("stack traceback") == std::string::npos) {
        const std::string chunkName = site.script != nullptr ? site.script->chunkName : std::string{};
        coroutineError += "\nstack traceback:\n\t[entry " + std::string{ functionName } + " in " + chunkName + "]";
    }
    const std::size_t traceOffset = coroutineError.find("\nstack traceback:");
    diagnostics.push_back(ScriptDiagnostic{
        .entity = context.Self(),
        .assetId = context.Asset(),
        .backend = context.Backend(),
        .message = coroutineError,
        .stackTrace = traceOffset == std::string::npos ? std::string{} : coroutineError.substr(traceOffset + 1U),
    });
    executed = false;
}

bool PucLuaScriptRuntime::ResolvePhaseCall(
    kb::scene::SceneEntity entity,
    const kb::scene::BehaviourComponent& behaviour,
    ScriptLifecycleEvent event,
    ScriptPhaseCall& call) {
    if (state_ == nullptr) {
        return false;
    }
    CallSite& site = SiteFor(InstanceKey{ entity.Id(), behaviour.behaviourAssetId });
    PhaseTarget& target = site.phases[static_cast<std::size_t>(event)];
    target = PhaseTarget{ .site = &site, .name = ToString(event), .nameReference = PhaseNameReference(event) };
    call = ScriptPhaseCall{ &PucLuaScriptRuntime::InvokePhase, &target };
    return true;
}

bool PucLuaScriptRuntime::ResolveEventCall(
    kb::scene::SceneEntity entity,
    const kb::scene::BehaviourComponent& behaviour,
    ScriptPhaseCall& call) {
    if (state_ == nullptr) {
        return false;
    }
    call = ScriptPhaseCall{ &PucLuaScriptRuntime::InvokeEventHandler, &SiteFor(InstanceKey{ entity.Id(), behaviour.behaviourAssetId }) };
    return true;
}

std::uint64_t PucLuaScriptRuntime::PhaseCallRevision() const noexcept {
    return callSiteRevision_;
}

bool PucLuaScriptRuntime::InvokePhase(
    void* target,
    const kb::scene::BehaviourComponent& behaviour,
    ScriptExecutionContext& context,
    std::vector<ScriptDiagnostic>& diagnostics) {
    const PhaseTarget& phase = *static_cast<const PhaseTarget*>(target);
    return InvokeEntry(*phase.site, phase.name, phase.nameReference, nullptr, behaviour, context, diagnostics);
}

bool PucLuaScriptRuntime::InvokeEventHandler(
    void* target,
    const kb::scene::BehaviourComponent& behaviour,
    ScriptExecutionContext& context,
    std::vector<ScriptDiagnostic>& diagnostics) {
    const ScriptEvent& event = *context.IncomingEvent();
    CallSite& site = *static_cast<CallSite*>(target);
    return InvokeEntry(site, event.name.c_str(), site.runtime->EventNameReference(event), &event, behaviour, context, diagnostics);
}

bool PucLuaScriptRuntime::InvokeEntry(
    CallSite& site,
    const char* name,
    int nameReference,
    const ScriptEvent* event,
    const kb::scene::BehaviourComponent& behaviour,
    ScriptExecutionContext& context,
    std::vector<ScriptDiagnostic>& diagnostics) {
    PucLuaScriptRuntime& runtime = *site.runtime;
    // What PucLuaExecutionContextScope does, without finding the state's slot again for every call.
    struct ActiveContext {
        PucLuaStateLifetime& lifetime;
        ScriptExecutionContext* previous;
        ~ActiveContext() { lifetime.activeContext = previous; }
    } activeContext{ *runtime.stateLifetime_, runtime.stateLifetime_->activeContext };
    runtime.stateLifetime_->activeContext = &context;
    bool executed = false;
    const std::size_t before = diagnostics.size();
    if (runtime.TryLeanExecute(site, name, nameReference, event, context, diagnostics, executed)) {
        return executed && diagnostics.size() == before;
    }
    ScriptBackendExecutionResult result = runtime.ExecuteFunction(behaviour, name, context, event);
    for (ScriptDiagnostic& diagnostic : result.diagnostics) {
        diagnostics.push_back(std::move(diagnostic));
    }
    return result.executed && diagnostics.size() == before;
}

PucLuaScriptRuntime::ScriptRecord* PucLuaScriptRuntime::FindScriptRecord(std::uint64_t assetId) noexcept {
    if (cachedScript_ != nullptr && cachedScriptAsset_ == assetId) {
        return cachedScript_;
    }
    const auto found = scripts_.find(assetId);
    if (found == scripts_.end()) {
        return nullptr;
    }
    cachedScriptAsset_ = assetId;
    cachedScript_ = &found->second;
    return cachedScript_;
}

int PucLuaScriptRuntime::PhaseNameReference(ScriptLifecycleEvent event) {
#if defined(KB_SCRIPT_VM_LUAJIT)
    int& reference = phaseNameReferences_[static_cast<std::size_t>(event)];
    if (reference == 0 && state_ != nullptr) {
        lua_pushstring(state_, ToString(event));
        reference = luaL_ref(state_, LUA_REGISTRYINDEX);
    }
    return reference;
#else
    static_cast<void>(event);
    return 0;
#endif
}

int PucLuaScriptRuntime::EventNameReference(const ScriptEvent& event) {
#if defined(KB_SCRIPT_VM_LUAJIT)
    if (eventBroadcasts_.empty() || eventBroadcasts_.back().event != &event || state_ == nullptr) {
        return 0;
    }
    EventBroadcast& broadcast = eventBroadcasts_.back();
    if (broadcast.nameReference == 0) {
        lua_pushlstring(state_, event.name.data(), event.name.size());
        broadcast.nameReference = luaL_ref(state_, LUA_REGISTRYINDEX);
    }
    return broadcast.nameReference;
#else
    static_cast<void>(event);
    return 0;
#endif
}

void PucLuaScriptRuntime::BeginEventBroadcast(const ScriptEvent& event) {
    eventBroadcasts_.push_back(EventBroadcast{ .event = &event, .tableReference = kNoReference });
}

void PucLuaScriptRuntime::EndEventBroadcast() noexcept {
    if (eventBroadcasts_.empty()) {
        return;
    }
    if (eventBroadcasts_.back().tableReference != kNoReference && state_ != nullptr) {
        luaL_unref(state_, LUA_REGISTRYINDEX, eventBroadcasts_.back().tableReference);
    }
    if (eventBroadcasts_.back().nameReference != 0 && state_ != nullptr) {
        luaL_unref(state_, LUA_REGISTRYINDEX, eventBroadcasts_.back().nameReference);
    }
    eventBroadcasts_.pop_back();
}

bool PucLuaScriptRuntime::HasEntry(const kb::scene::BehaviourComponent& behaviour, std::string_view name) {
    if (state_ == nullptr || !coroutineRefs_.empty()) {
        return true;
    }
    const auto script = scripts_.find(behaviour.behaviourAssetId);
    if (script == scripts_.end()) {
        return true; // the dispatch itself reports a script that is not loaded
    }
    PucLuaStackGuard stack{ state_ };
    lua_rawgeti(state_, LUA_REGISTRYINDEX, script->second.environmentRef);
    lua_pushlstring(state_, name.data(), name.size());
    lua_gettable(state_, -2);
    return lua_isnil(state_, -1) == 0; // a value that is not a function still dispatches, to be reported
}

void PucLuaScriptRuntime::StepGarbageCollector() noexcept {
    const std::uint32_t budgetMicroseconds = executionBudgetSettings_.luaCollectionMicrosecondsPerFrame;
    if (state_ == nullptr || budgetMicroseconds == 0U || executionBudgetActive_ || memoryBudget_->protectedDepth != 0U) {
        return;
    }
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
#if defined(KB_SCRIPT_VM_LUAJIT)
    // Incremental: one slice of collector work (its size is bounded by the VM's step size); the allocations of the scripts
    // pay for the rest as they go, so the allowance is never the reason to keep collecting.
    static_cast<void>(lua_gc(state_, LUA_GCSTEP, 0));
#else
    // Generational: a step collects the objects created since the last one, the temporary tables of a frame's
    // scripts, instead of marking the whole heap again; the mode is applied once and stays.
    static_cast<void>(lua_gc(state_, LUA_GCGEN, 0, 0));
    static_cast<void>(lua_gc(state_, LUA_GCSTEP, 0));
#endif
    const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
    ++collectionStats_.steps;
    collectionStats_.totalMicroseconds += elapsed;
    collectionStats_.longestStepMicroseconds = std::max(collectionStats_.longestStepMicroseconds, elapsed);
}

PucLuaCollectionStats PucLuaScriptRuntime::CollectionStats() const noexcept {
    return collectionStats_;
}

lua_State* PucLuaScriptRuntime::AcquireThread() {
    PucLuaStateLifetime& lifetime = *stateLifetime_;
    if (lifetime.spareThread != nullptr && !lifetime.spareThreadBusy) {
        lua_rawgeti(state_, LUA_REGISTRYINDEX, lifetime.spareThreadReference);
        lifetime.spareThreadBusy = true;
        return lifetime.spareThread;
    }
    lua_State* thread = nullptr;
    if (lua_rawgetp(state_, LUA_REGISTRYINDEX, &kThreadPoolKey) == LUA_TTABLE) {
        const lua_Unsigned idle = lua_rawlen(state_, -1);
        if (idle != 0U) {
            lua_rawgeti(state_, -1, static_cast<lua_Integer>(idle));
            lua_pushnil(state_);
            lua_rawseti(state_, -3, static_cast<lua_Integer>(idle));
            lua_remove(state_, -2);
            thread = lua_tothread(state_, -1);
        }
    }
    if (thread == nullptr) {
        lua_pop(state_, 1);
        thread = lua_newthread(state_);
    }
    if (lifetime.spareThread == nullptr) {
        // This thread becomes the one that is kept: anchored once, handed out again after every finished entry.
        lua_pushvalue(state_, -1);
        lifetime.spareThreadReference = luaL_ref(state_, LUA_REGISTRYINDEX);
        lifetime.spareThread = thread;
        lifetime.spareThreadBusy = true;
    }
    return thread;
}

void PucLuaScriptRuntime::ReleaseThread(lua_State* thread, int threadIndex) noexcept {
    lua_settop(thread, 0);
    if (thread == stateLifetime_->spareThread) {
        stateLifetime_->spareThreadBusy = false;
        return;
    }
    threadIndex = lua_absindex(state_, threadIndex);
    if (lua_rawgetp(state_, LUA_REGISTRYINDEX, &kThreadPoolKey) != LUA_TTABLE) {
        lua_pop(state_, 1);
        lua_createtable(state_, static_cast<int>(kMaxIdleThreads), 0);
        lua_pushvalue(state_, -1);
        lua_rawsetp(state_, LUA_REGISTRYINDEX, &kThreadPoolKey);
    }
    const lua_Unsigned idle = lua_rawlen(state_, -1);
    if (idle < kMaxIdleThreads) {
        lua_pushvalue(state_, threadIndex);
        lua_rawseti(state_, -2, static_cast<lua_Integer>(idle + 1U));
    }
    lua_pop(state_, 1);
}

ScriptBackendExecutionResult PucLuaScriptRuntime::ExecuteFunction(
    const kb::scene::BehaviourComponent& behaviour,
    std::string_view functionName,
    ScriptExecutionContext& context,
    const ScriptEvent* event) {
    PucLuaExecutionContextScope activeContext{state_, context};
    ScriptBackendExecutionResult result{};
    const kb::assets::AssetId assetId{ behaviour.behaviourAssetId };
    const InstanceKey instanceKey{
        .entityId = context.Self().Id(),
        .assetId = assetId.value,
    };
    const auto releaseSelf = [&]() {
        if (const auto self = stateLifetime_->selfReferences.find(PucLuaSelfKey{ instanceKey.entityId, instanceKey.assetId });
            self != stateLifetime_->selfReferences.end()) {
            luaL_unref(state_, LUA_REGISTRYINDEX, self->second);
            stateLifetime_->selfReferences.erase(self);
            if (const auto site = callSites_.find(instanceKey); site != callSites_.end()) {
                site->second.selfReference = kNoReference;
            }
        }
    };
    if (context.Lifecycle() == ScriptLifecycleEvent::Created) {
        // The object starts over: nothing an earlier start left in its self is carried into this one.
        releaseSelf();
    }
    const auto eraseDestroyedInstanceState = [&]() {
        if (context.Lifecycle() == ScriptLifecycleEvent::Destroyed) {
            instanceVariables_.erase(instanceKey);
            releaseSelf();
            callSites_.erase(instanceKey);
            // Calls kept for the site (phase lists, broadcast receivers) must be resolved again.
            ++callSiteRevision_;
            ClearCoroutines(instanceKey);
            if (ScriptEventBus* events = context.Events(); events != nullptr) {
                ClearEventSubscriptions(instanceKey, *events);
            }
        }
    };
    if (const ScriptLifecycleEvent lifecycle = context.Lifecycle();
        lifecycle != ScriptLifecycleEvent::Created && lifecycle != ScriptLifecycleEvent::Destroyed) {
        bool executed = false;
        if (TryLeanExecute(SiteFor(instanceKey), functionName.data(), 0, event, context, result.diagnostics, executed)) {
            result.executed = executed;
            return result;
        }
    }
    ScriptRecord* const script = FindScriptRecord(assetId.value);
    if (script == nullptr) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = "lua script is not loaded",
        });
        eraseDestroyedInstanceState();
        return result;
    }
    const int environmentRef = script->environmentRef;
    if (debugPause_.has_value() && debugPause_->instance == instanceKey && debugPause_->functionName == functionName &&
        context.Lifecycle() != ScriptLifecycleEvent::Destroyed) {
        // Suspended at a debugger pause: the entry stays on its line until Continue or a step releases it, while
        // the rest of the game and the other scripts keep running.
        return result;
    }

    // An instance is brought in line with its asset's exposed-variable definitions when they change (and on its
    // first call), not on every call: a variable a script wrote is marked overridden and keeps its value, any
    // other one always holds the default.
    if (const auto definitions = exposedVariables_.empty() ? exposedVariables_.end() : exposedVariables_.find(assetId.value);
        definitions != exposedVariables_.end()) {
        InstanceVariableSet& instance = instanceVariables_[instanceKey];
        if (instance.syncedRevision != exposedRevision_) {
            std::vector<PucLuaExposedVariableInstance>& variables = instance.values;
            std::vector<std::string_view> declaredNames;
            declaredNames.reserve(definitions->second.size());
            for (const ExposedVariableRecord& definition : definitions->second) {
                declaredNames.push_back(definition.pin.name);
                PucLuaExposedVariableInstance* variable = PucLuaValueBridge::FindVariable(variables, definition.pin.name);
                if (variable == nullptr) {
                    const ScriptValue value = definition.hasDefault ? definition.defaultValue : PucLuaValueBridge::DefaultFor(definition.pin.type);
                    variables.push_back(PucLuaExposedVariableInstance{
                        .name = definition.pin.name,
                        .type = definition.pin.type,
                        .value = value,
                        .overridden = false,
                    });
                    continue;
                }
                if (variable->type != definition.pin.type) {
                    if (variable->overridden) {
                        result.diagnostics.push_back(ScriptDiagnostic{
                            .entity = context.Self(),
                            .assetId = assetId,
                            .backend = behaviour.backend,
                            .message = "lua exposed variable type conflict for '" + variable->name + "'",
                        });
                        eraseDestroyedInstanceState();
                        return result;
                    }
                    const ScriptValue value = definition.hasDefault ? definition.defaultValue : PucLuaValueBridge::DefaultFor(definition.pin.type);
                    variable->type = definition.pin.type;
                    variable->value = value;
                    variable->overridden = false;
                    continue;
                }
                if (!variable->overridden) {
                    variable->value = definition.hasDefault ? definition.defaultValue : PucLuaValueBridge::DefaultFor(definition.pin.type);
                }
            }
            variables.erase(
                std::remove_if(variables.begin(), variables.end(), [&declaredNames](const PucLuaExposedVariableInstance& variable) {
                    return !variable.overridden && std::ranges::find(declaredNames, std::string_view{ variable.name }) == declaredNames.end();
                }),
                variables.end());
            instance.syncedRevision = exposedRevision_;
        }
    }

    PucLuaStackGuard stack{ state_ };
    lua_rawgeti(state_, LUA_REGISTRYINDEX, environmentRef);
    const int environmentIndex = lua_gettop(state_);
    if (std::optional<std::string> error = PucLuaRuntimeApi::AttachExecutionApi(state_, environmentIndex, context, *this, &script->attachedStamp)) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = std::move(*error),
        });
        eraseDestroyedInstanceState();
        return result;
    }

    // LIB-097: every Lua lifecycle/event entry is a generator. A yielded
    // thread is resumed on the next invocation of that same entry; a normal
    // function still runs exactly once and its thread goes back to the idle
    // pool. Only a thread that yields is anchored in the registry.
    lua_State* coroutine = nullptr;
    int coroutineRef = kNoReference;
    bool resuming = false;
    if (!coroutineRefs_.empty()) {
        if (const auto instance = coroutineRefs_.find(instanceKey); instance != coroutineRefs_.end()) {
            if (const auto suspended = instance->second.find(std::string{functionName}); suspended != instance->second.end()) {
                coroutineRef = suspended->second;
                lua_rawgeti(state_, LUA_REGISTRYINDEX, coroutineRef);
                coroutine = lua_tothread(state_, -1);
                resuming = coroutine != nullptr;
                if (!resuming) {
                    lua_pop(state_, 1);
                }
            }
        }
    }

    int argumentCount = 0;
    bool resumingInsideHook = false;
    int threadIndex = 0;
    if (coroutine == nullptr) {
        // Both callers pass a name that ends with a null character (a literal or a std::string), so the lookup goes
        // through Lua's cache of C strings instead of interning the name again on every call.
        lua_getfield(state_, environmentIndex, functionName.data());
        if (lua_isnil(state_, -1) != 0) {
            eraseDestroyedInstanceState();
            return result;
        }
        if (lua_isfunction(state_, -1) == 0) {
            result.diagnostics.push_back(ScriptDiagnostic{
                .entity = context.Self(),
                .assetId = assetId,
                .backend = behaviour.backend,
                .message = "lua script entry is not a function",
            });
            eraseDestroyedInstanceState();
            return result;
        }
        coroutine = AcquireThread();
        threadIndex = lua_gettop(state_);
        // The stack now holds the environment, the entry function and the thread. The function is copied to the
        // thread before its first resume.
        lua_pushvalue(state_, threadIndex - 1);
        lua_xmove(state_, coroutine, 1);
    } else {
        threadIndex = lua_gettop(state_);
        resumingInsideHook = SuspendedInsideHook(coroutine);
        if (!resumingInsideHook) {
            lua_settop(coroutine, 0);
        }
    }

    if (!resumingInsideHook) {
        // `self` is one table for the whole life of the object: made on its first call, then read from the registry. An
        // object that holds exposed variables gets their current values copied into it on every call.
        const PucLuaSelfKey selfKey{ instanceKey.entityId, instanceKey.assetId };
        if (const auto cached = stateLifetime_->selfReferences.find(selfKey); cached != stateLifetime_->selfReferences.end()) {
            lua_rawgeti(state_, LUA_REGISTRYINDEX, cached->second);
            if (!instanceVariables_.empty() && instanceVariables_.contains(instanceKey)) {
                PucLuaRuntimeApi::RefreshSelfVariables(state_, -1, context);
            }
        } else {
            PucLuaRuntimeApi::PushSelf(state_, context);
            lua_pushvalue(state_, -1);
            const int selfReference = luaL_ref(state_, LUA_REGISTRYINDEX);
            stateLifetime_->selfReferences.emplace(selfKey, selfReference);
            if (const auto site = callSites_.find(instanceKey); site != callSites_.end()) {
                site->second.selfReference = selfReference;
            }
        }
        ++argumentCount;
        if (event == nullptr) {
            lua_pushnumber(state_, static_cast<lua_Number>(context.DeltaSeconds()));
        } else {
            PushEventValue(state_, *event);
        }
        ++argumentCount;
        lua_xmove(state_, coroutine, argumentCount);
    }

    int resultCount = 0;
    const bool executionBudgetEnabled = IsExecutionBudgetEnabled();
    if (executionBudgetEnabled) {
        BeginExecutionBudget();
    }
    PucLuaDebugHook::Install(coroutine, *this);
    debugSuspendAllowed_ = context.Lifecycle() != ScriptLifecycleEvent::Destroyed;
    const int status = [&] {
        const PucLuaMemoryLimitScope memoryLimit{ coroutine };
        return lua_resume(coroutine, state_, argumentCount, &resultCount);
    }();
    debugSuspendAllowed_ = false;
    std::string coroutineError =
        status == LUA_OK || status == LUA_YIELD ? std::string{} : PucLuaErrorReporter::ErrorWithTracebackFromTop(coroutine);
    if (status != LUA_OK && status != LUA_YIELD && coroutineError.find("stack traceback") == std::string::npos) {
        const auto chunkName = std::string_view{ script->chunkName };
        coroutineError += "\nstack traceback:\n\t[entry " + std::string{ functionName } + " in " + std::string{ chunkName } + "]";
    }
    PucLuaDebugHook::Clear(coroutine);
    if (executionBudgetEnabled) {
        EndExecutionBudget();
    }
    const bool debugSuspended = status == LUA_YIELD && debugSuspendThread_ == coroutine;
    if (debugSuspendThread_ == coroutine) {
        debugSuspendThread_ = nullptr;
    }
    if (status != LUA_YIELD && debugStepThread_ == coroutine) {
        // The stepped entry returned or failed: there is no next line of it left to stop on.
        debugStepMode_ = DebugStepMode::Run;
        debugStepThread_ = nullptr;
    }
    if (coroutine == stateLifetime_->spareThread && status != LUA_OK) {
        // A thread that yielded stays anchored by its own reference; one that failed is not run again.
        luaL_unref(state_, LUA_REGISTRYINDEX, stateLifetime_->spareThreadReference);
        stateLifetime_->spareThread = nullptr;
        stateLifetime_->spareThreadReference = -2;
        stateLifetime_->spareThreadBusy = false;
    }
    if (status == LUA_YIELD) {
        if (coroutineRef == kNoReference) {
            lua_pushvalue(state_, threadIndex);
            coroutineRef = luaL_ref(state_, LUA_REGISTRYINDEX);
        }
        coroutineRefs_[instanceKey][std::string{functionName}] = coroutineRef;
        if (debugSuspended) {
            debugPause_ = DebugPause{
                .thread = coroutine,
                .instance = instanceKey,
                .functionName = std::string{ functionName },
                .depth = debugSuspendDepth_,
            };
        }
        result.executed = true;
        if (context.Lifecycle() == ScriptLifecycleEvent::Destroyed) {
            // Destroyed is terminal: there is no future lifecycle invocation
            // on which this generator could resume. Retaining it would leak
            // both the Lua thread and per-instance exposed state indefinitely.
            eraseDestroyedInstanceState();
            result.diagnostics.push_back(ScriptDiagnostic{
                .entity = context.Self(),
                .assetId = assetId,
                .backend = behaviour.backend,
                .message = "lua Destroyed lifecycle cannot yield because the behaviour is being permanently released",
            });
        }
        return result;
    }

    if (resuming) {
        if (const auto instance = coroutineRefs_.find(instanceKey); instance != coroutineRefs_.end()) {
            if (const auto suspended = instance->second.find(std::string{functionName}); suspended != instance->second.end()) {
                luaL_unref(state_, LUA_REGISTRYINDEX, suspended->second);
                instance->second.erase(suspended);
                if (instance->second.empty()) {
                    coroutineRefs_.erase(instance);
                }
            }
        }
    }
    if (status != LUA_OK) {
        const std::size_t traceOffset = coroutineError.find("\nstack traceback:");
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = coroutineError,
            .stackTrace = traceOffset == std::string::npos ? std::string{}
                : coroutineError.substr(traceOffset + 1U),
        });
        eraseDestroyedInstanceState();
        return result;
    }

    ReleaseThread(coroutine, threadIndex);
    result.executed = true;
    eraseDestroyedInstanceState();
    return result;
}

int PucLuaScriptRuntime::FindScriptEnvironment(kb::assets::AssetId assetId) const noexcept {
    const auto iter = scripts_.find(assetId.value);
    return iter == scripts_.end() ? kNoReference : iter->second.environmentRef;
}

void PucLuaScriptRuntime::ClearCoroutines(const InstanceKey& instanceKey) noexcept {
    if (debugPause_.has_value() && debugPause_->instance == instanceKey) {
        debugPause_.reset();
    }
    const auto iter = coroutineRefs_.find(instanceKey);
    if (iter == coroutineRefs_.end()) {
        return;
    }
    if (state_ != nullptr) {
        for (const auto& [functionName, reference] : iter->second) {
            static_cast<void>(functionName);
            lua_rawgeti(state_, LUA_REGISTRYINDEX, reference);
            if (lua_tothread(state_, -1) == debugStepThread_) {
                debugStepMode_ = DebugStepMode::Run;
                debugStepThread_ = nullptr;
            }
            lua_pop(state_, 1);
            luaL_unref(state_, LUA_REGISTRYINDEX, reference);
        }
    }
    coroutineRefs_.erase(iter);
}

void PucLuaScriptRuntime::ClearCoroutinesForAsset(kb::assets::AssetId assetId) noexcept {
    std::vector<InstanceKey> keys;
    for (const auto& [key, functions] : coroutineRefs_) {
        static_cast<void>(functions);
        if (key.assetId == assetId.value) {
            keys.push_back(key);
        }
    }
    for (const InstanceKey& key : keys) {
        ClearCoroutines(key);
    }
}

void PucLuaScriptRuntime::TrackEventSubscription(const InstanceKey& instanceKey, EventSubscriptionHandle handle) {
    if (handle != kInvalidEventSubscriptionHandle) {
        eventSubscriptionHandles_[instanceKey].push_back(handle);
    }
}

void PucLuaScriptRuntime::TrackEventSubscription(
    kb::scene::SceneEntity entity,
    kb::assets::AssetId assetId,
    EventSubscriptionHandle handle) {
    TrackEventSubscription(InstanceKey{
        .entityId = entity.Id(),
        .assetId = assetId.value,
    }, handle);
}

void PucLuaScriptRuntime::ClearEventSubscriptions(const InstanceKey& instanceKey, ScriptEventBus& events) noexcept {
    const auto iterator = eventSubscriptionHandles_.find(instanceKey);
    if (iterator == eventSubscriptionHandles_.end()) {
        return;
    }
    for (const EventSubscriptionHandle handle : iterator->second) {
        static_cast<void>(events.Unsubscribe(handle));
    }
    eventSubscriptionHandles_.erase(iterator);
}

void PucLuaScriptRuntime::ClearEventSubscriptionsForAsset(kb::assets::AssetId assetId, ScriptEventBus& events) noexcept {
    std::vector<InstanceKey> keys;
    for (const auto& [key, handles] : eventSubscriptionHandles_) {
        static_cast<void>(handles);
        if (key.assetId == assetId.value) {
            keys.push_back(key);
        }
    }
    for (const InstanceKey& key : keys) {
        ClearEventSubscriptions(key, events);
    }
}

void PucLuaScriptRuntime::ResetAssetForHotReload(kb::assets::AssetId assetId, ScriptEventBus& events) noexcept {
    ClearEventSubscriptionsForAsset(assetId, events);
}

} // namespace kb::script
