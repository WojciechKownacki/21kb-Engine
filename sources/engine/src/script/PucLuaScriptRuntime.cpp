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
    PucLuaRuntimeApi::AttachRuntimeFunctions(state_, environmentIndex, *this);

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
    }
    scripts_.clear();
    modules_.clear();
    exposedVariables_.clear();
    instanceVariables_.clear();
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

void PucLuaScriptRuntime::SetDebugSettings(PucLuaDebugSettings settings) {
    debugSettings_ = std::move(settings);
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

bool PucLuaScriptRuntime::NeedsDebugHook() const noexcept {
    return debugStepMode_ == DebugStepMode::BreakOnNextLine ||
           debugStepMode_ == DebugStepMode::StepInto ||
           (debugSettings_.enableBreakpoints && !debugSettings_.breakpoints.empty());
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
    PucLuaRuntimeApi::AttachRuntimeFunctions(state_, environmentIndex, *this);
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

ScriptBackendExecutionResult PucLuaScriptRuntime::ExecuteEvent(const kb::scene::BehaviourComponent& behaviour, const ScriptEvent& event, ScriptExecutionContext& context) {
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
    PucLuaRuntimeApi::AttachExecutionApi(state_, environmentIndex, context, *this);
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

    lua_pushcfunction(state_, &PucLuaErrorReporter::Traceback);
    const int errorHandlerIndex = lua_gettop(state_) - 1;
    lua_insert(state_, errorHandlerIndex);

    PucLuaRuntimeApi::PushSelf(state_, context);
    int argCount = 1;
    if (event == nullptr) {
        lua_pushnumber(state_, static_cast<lua_Number>(context.DeltaSeconds()));
        ++argCount;
    } else {
        PucLuaRuntimeApi::PushEvent(state_, *event);
        ++argCount;
    }

    PucLuaDebugHook::Install(state_, *this);
    const int status = lua_pcall(state_, argCount, 0, errorHandlerIndex);
    PucLuaDebugHook::Clear(state_);
    if (status != LUA_OK) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = PucLuaErrorReporter::ErrorFromTop(state_),
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

} // namespace kb::script
