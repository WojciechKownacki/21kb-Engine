#include "engine/script/PucLuaScriptRuntime.hpp"

#include "script/lua/PucLuaDebugHook.hpp"
#include "script/lua/PucLuaRuntimeApi.hpp"
#include "script/lua/PucLuaSandboxEnvironment.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"
#include "script/lua/PucLuaValueBridge.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

constexpr int kNoReference = LUA_NOREF;

} // namespace

PucLuaScriptRuntime::PucLuaScriptRuntime()
    : state_(luaL_newstate()) {
    if (state_ != nullptr) {
        *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(state_)) = this;
        PucLuaSandboxEnvironment::OpenSafeLibraries(state_);
    }
}

PucLuaScriptRuntime::~PucLuaScriptRuntime() {
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

    const std::string chunk = chunkName.empty() ? std::string{ "lua-script-" } + std::to_string(assetId.value) : std::string{ chunkName };
    if (luaL_loadbufferx(state_, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK) {
        return PucLuaLoadResult{ .error = PucLuaErrorReporter::ErrorFromTop(state_) };
    }

    lua_pushvalue(state_, environmentIndex);
    static_cast<void>(lua_setupvalue(state_, -2, 1));
    lua_pushcfunction(state_, &PucLuaErrorReporter::Traceback);
    const int errorHandlerIndex = lua_gettop(state_) - 1;
    lua_insert(state_, errorHandlerIndex);
    PucLuaDebugHook::Install(state_, *this);
    const int status = lua_pcall(state_, 0, 0, errorHandlerIndex);
    PucLuaDebugHook::Clear(state_);
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
    }
    scripts_.clear();
    modules_.clear();
    exposedVariables_.clear();
    instanceVariables_.clear();
    coroutineRefs_.clear();
    eventSubscriptionHandles_.clear();
    lastDebugPause_ = {};
    debugStepMode_ = DebugStepMode::Run;
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
    UnloadModule(name);
    modules_[name] = ModuleRecord{
        .source = std::move(source),
        .chunkName = chunkName.empty() ? name : std::move(chunkName),
        .valueRef = kNoReference,
        .contentHash = contentHash,
        .generation = generation_++,
    };
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
}

std::span<const PucLuaExposedVariableInstance> PucLuaScriptRuntime::InstanceVariables(kb::scene::SceneEntity entity, kb::assets::AssetId assetId) const noexcept {
    const auto iter = instanceVariables_.find(InstanceKey{
        .entityId = entity.Id(),
        .assetId = assetId.value,
    });
    if (iter == instanceVariables_.end()) {
        return {};
    }
    return iter->second;
}

bool PucLuaScriptRuntime::SetInstanceVariable(kb::scene::SceneEntity entity, kb::assets::AssetId assetId, std::string_view name, ScriptValue value) {
    const auto iter = instanceVariables_.find(InstanceKey{
        .entityId = entity.Id(),
        .assetId = assetId.value,
    });
    if (iter == instanceVariables_.end()) {
        return false;
    }
    PucLuaExposedVariableInstance* variable = PucLuaValueBridge::FindVariable(iter->second, name);
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
    }];
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
}

void PucLuaScriptRuntime::BeginExecutionBudget() noexcept {
    remainingLuaInstructions_ = executionBudgetSettings_.luaInstructionsPerBehaviour;
    executionBudgetActive_ = true;
}

void PucLuaScriptRuntime::EndExecutionBudget() noexcept {
    remainingLuaInstructions_ = 0U;
    executionBudgetActive_ = false;
}

bool PucLuaScriptRuntime::ConsumeLuaInstructions(std::size_t count) noexcept {
    if (!executionBudgetActive_) return true;
    if (count > remainingLuaInstructions_) return false;
    remainingLuaInstructions_ -= count;
    return true;
}

kb::core::BudgetExceededPolicy PucLuaScriptRuntime::ExecutionBudgetPolicy() const noexcept {
    return executionBudgetSettings_.policy;
}

bool PucLuaScriptRuntime::IsExecutionBudgetEnabled() const noexcept {
    return executionBudgetSettings_.luaInstructionsPerBehaviour != 0U;
}

bool PucLuaScriptRuntime::HasActiveExecutionBudget() const noexcept {
    return executionBudgetActive_ && IsExecutionBudgetEnabled();
}

const PucLuaDebugSettings& PucLuaScriptRuntime::DebugSettings() const noexcept {
    return debugSettings_;
}

void PucLuaScriptRuntime::RequestBreakOnNextLine() noexcept {
    debugStepMode_ = DebugStepMode::BreakOnNextLine;
}

void PucLuaScriptRuntime::RequestStepInto() noexcept {
    debugStepMode_ = DebugStepMode::StepInto;
}

void PucLuaScriptRuntime::ResumeDebugExecution() noexcept {
    debugStepMode_ = DebugStepMode::Run;
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
        return PucLuaDebugPauseReason::Step;
    case DebugStepMode::Run:
        break;
    }
    return std::nullopt;
}

bool PucLuaScriptRuntime::NeedsDebugLineHook() const noexcept {
    return debugStepMode_ == DebugStepMode::BreakOnNextLine ||
           debugStepMode_ == DebugStepMode::StepInto ||
           (debugSettings_.enableBreakpoints && !debugSettings_.breakpoints.empty());
}

bool PucLuaScriptRuntime::NeedsDebugHook() const noexcept {
    return NeedsDebugLineHook() || HasActiveExecutionBudget();
}

const PucLuaDebugPauseSnapshot& PucLuaScriptRuntime::LastDebugPause() const noexcept {
    return lastDebugPause_;
}

void PucLuaScriptRuntime::ClearDebugPause() noexcept {
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
    static_cast<void>(lua_setupvalue(state_, -2, 1));
    lua_pushcfunction(state_, &PucLuaErrorReporter::Traceback);
    const int errorHandlerIndex = lua_gettop(state_) - 1;
    lua_insert(state_, errorHandlerIndex);
    PucLuaDebugHook::Install(state_, *this);
    const int status = lua_pcall(state_, 0, 1, errorHandlerIndex);
    PucLuaDebugHook::Clear(state_);
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

ScriptBackendExecutionResult PucLuaScriptRuntime::ExecuteFunction(
    const kb::scene::BehaviourComponent& behaviour,
    std::string_view functionName,
    ScriptExecutionContext& context,
    const ScriptEvent* event) {
    ScriptBackendExecutionResult result{};
    const kb::assets::AssetId assetId{ behaviour.behaviourAssetId };
    const InstanceKey instanceKey{
        .entityId = context.Self().Id(),
        .assetId = assetId.value,
    };
    const auto eraseDestroyedInstanceState = [&]() {
        if (context.Lifecycle() == ScriptLifecycleEvent::Destroyed) {
            instanceVariables_.erase(instanceKey);
            ClearCoroutines(instanceKey);
            if (ScriptEventBus* events = context.Events(); events != nullptr) {
                ClearEventSubscriptions(instanceKey, *events);
            }
        }
    };
    const int environmentRef = FindScriptEnvironment(assetId);
    if (environmentRef == kNoReference) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = "lua script is not loaded",
        });
        eraseDestroyedInstanceState();
        return result;
    }

    if (const auto definitions = exposedVariables_.find(assetId.value); definitions != exposedVariables_.end()) {
        std::vector<PucLuaExposedVariableInstance>& variables = instanceVariables_[instanceKey];
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
    }

    PucLuaStackGuard stack{ state_ };
    lua_rawgeti(state_, LUA_REGISTRYINDEX, environmentRef);
    const int environmentIndex = lua_gettop(state_);
    if (std::optional<std::string> error = PucLuaRuntimeApi::AttachExecutionApi(state_, environmentIndex, context, *this)) {
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
    // function still runs exactly once and then releases its thread.
    lua_State* coroutine = nullptr;
    int coroutineRef = kNoReference;
    bool resuming = false;
    if (const auto instance = coroutineRefs_.find(instanceKey); instance != coroutineRefs_.end()) {
        if (const auto suspended = instance->second.find(std::string{functionName}); suspended != instance->second.end()) {
            coroutineRef = suspended->second;
            lua_rawgeti(state_, LUA_REGISTRYINDEX, coroutineRef);
            coroutine = lua_tothread(state_, -1);
            lua_pop(state_, 1);
            resuming = coroutine != nullptr;
        }
    }

    int argumentCount = 0;
    if (coroutine == nullptr) {
        lua_getfield(state_, environmentIndex, std::string{ functionName }.c_str());
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
        coroutine = lua_newthread(state_);
        *static_cast<PucLuaScriptRuntime**>(lua_getextraspace(coroutine)) = this;
        lua_pushvalue(state_, -1);
        coroutineRef = luaL_ref(state_, LUA_REGISTRYINDEX);
        lua_pop(state_, 1);
        // Stack now holds environment and the entry function. Move the
        // function to the owned generator thread before its first resume.
        lua_xmove(state_, coroutine, 1);
    } else if (resuming) {
        lua_settop(coroutine, 0);
    }

    PucLuaRuntimeApi::PushSelf(state_, context);
    ++argumentCount;
    if (event == nullptr) {
        lua_pushnumber(state_, static_cast<lua_Number>(context.DeltaSeconds()));
    } else {
        PucLuaRuntimeApi::PushEvent(state_, *event);
    }
    ++argumentCount;
    lua_xmove(state_, coroutine, argumentCount);

    int resultCount = 0;
    const bool executionBudgetEnabled = IsExecutionBudgetEnabled();
    if (executionBudgetEnabled) {
        BeginExecutionBudget();
    }
    PucLuaDebugHook::Install(coroutine, *this);
    const int status = lua_resume(coroutine, state_, argumentCount, &resultCount);
    std::string coroutineError =
        status == LUA_OK ? std::string{} : PucLuaErrorReporter::ErrorWithTracebackFromTop(coroutine);
    if (status != LUA_OK && coroutineError.find("stack traceback") == std::string::npos) {
        const auto script = scripts_.find(assetId.value);
        const std::string_view chunkName = script == scripts_.end() ? std::string_view{ "<unknown>" } : std::string_view{ script->second.chunkName };
        coroutineError += "\nstack traceback:\n\t[entry " + std::string{ functionName } + " in " + std::string{ chunkName } + "]";
    }
    PucLuaDebugHook::Clear(coroutine);
    if (executionBudgetEnabled) {
        EndExecutionBudget();
    }
    if (status == LUA_YIELD) {
        coroutineRefs_[instanceKey][std::string{functionName}] = coroutineRef;
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

    if (const auto instance = coroutineRefs_.find(instanceKey); instance != coroutineRefs_.end()) {
        if (const auto suspended = instance->second.find(std::string{functionName}); suspended != instance->second.end()) {
            luaL_unref(state_, LUA_REGISTRYINDEX, suspended->second);
            instance->second.erase(suspended);
            if (instance->second.empty()) {
                coroutineRefs_.erase(instance);
            }
        } else if (coroutineRef != kNoReference) {
            luaL_unref(state_, LUA_REGISTRYINDEX, coroutineRef);
        }
    } else if (coroutineRef != kNoReference) {
        luaL_unref(state_, LUA_REGISTRYINDEX, coroutineRef);
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

    result.executed = true;
    eraseDestroyedInstanceState();
    return result;
}

int PucLuaScriptRuntime::FindScriptEnvironment(kb::assets::AssetId assetId) const noexcept {
    const auto iter = scripts_.find(assetId.value);
    return iter == scripts_.end() ? kNoReference : iter->second.environmentRef;
}

void PucLuaScriptRuntime::ClearCoroutines(const InstanceKey& instanceKey) noexcept {
    const auto iter = coroutineRefs_.find(instanceKey);
    if (iter == coroutineRefs_.end()) {
        return;
    }
    if (state_ != nullptr) {
        for (const auto& [functionName, reference] : iter->second) {
            static_cast<void>(functionName);
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
