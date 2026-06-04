#include "engine/script/PucLuaScriptRuntime.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <utility>

namespace kb::script {
namespace {

constexpr int kNoReference = LUA_NOREF;

class LuaStackScope final {
public:
    explicit LuaStackScope(lua_State* state) noexcept
        : state_(state)
        , top_(lua_gettop(state)) {}

    ~LuaStackScope() {
        lua_settop(state_, top_);
    }

    LuaStackScope(const LuaStackScope&) = delete;
    LuaStackScope& operator=(const LuaStackScope&) = delete;

private:
    lua_State* state_ = nullptr;
    int top_ = 0;
};

[[nodiscard]] std::string ErrorFromTop(lua_State* state) {
    const char* error = lua_tostring(state, -1);
    return error == nullptr ? std::string{"lua error"} : std::string{error};
}

void OpenSafeLibraries(lua_State* state) {
    luaL_requiref(state, "_G", luaopen_base, 1);
    lua_pop(state, 1);
    lua_pushnil(state);
    lua_setglobal(state, "dofile");
    lua_pushnil(state);
    lua_setglobal(state, "loadfile");
    luaL_requiref(state, "coroutine", luaopen_coroutine, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "table", luaopen_table, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "string", luaopen_string, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "math", luaopen_math, 1);
    lua_pop(state, 1);
    luaL_requiref(state, "utf8", luaopen_utf8, 1);
    lua_pop(state, 1);
}

[[nodiscard]] ScriptExecutionContext* ContextFromUpvalue(lua_State* state) noexcept {
    return static_cast<ScriptExecutionContext*>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] ScriptValue ValueFromLua(lua_State* state, int index) {
    switch (lua_type(state, index)) {
    case LUA_TBOOLEAN:
        return ScriptValue{lua_toboolean(state, index) != 0};
    case LUA_TNUMBER:
        if (lua_isinteger(state, index) != 0) {
            return ScriptValue{static_cast<int>(lua_tointeger(state, index))};
        }
        return ScriptValue{static_cast<float>(lua_tonumber(state, index))};
    case LUA_TSTRING:
        return ScriptValue{std::string{lua_tostring(state, index)}};
    default:
        break;
    }
    return ScriptValue{};
}

[[nodiscard]] std::vector<ScriptEventArgument> ArgumentsFromLuaTable(lua_State* state, int index) {
    std::vector<ScriptEventArgument> arguments;
    if (lua_istable(state, index) == 0) {
        return arguments;
    }

    const int absoluteIndex = lua_absindex(state, index);
    lua_pushnil(state);
    while (lua_next(state, absoluteIndex) != 0) {
        if (lua_type(state, -2) == LUA_TSTRING) {
            arguments.push_back(ScriptEventArgument{
                .name = lua_tostring(state, -2),
                .value = ValueFromLua(state, -1),
            });
        }
        lua_pop(state, 1);
    }
    return arguments;
}

int LuaEmit(lua_State* state) {
    ScriptExecutionContext* context = ContextFromUpvalue(state);
    if (context == nullptr) {
        return 0;
    }
    const char* eventName = luaL_checkstring(state, 1);
    std::vector<ScriptEventArgument> arguments;
    if (lua_gettop(state) >= 2 && lua_istable(state, 2) != 0) {
        arguments = ArgumentsFromLuaTable(state, 2);
    }
    context->Emit(eventName, std::move(arguments));
    return 0;
}

void PushScriptValue(lua_State* state, const ScriptValue& value) {
    switch (value.Type()) {
    case ScriptValueType::Bool:
        lua_pushboolean(state, value.AsBool() ? 1 : 0);
        break;
    case ScriptValueType::Int:
        lua_pushinteger(state, static_cast<lua_Integer>(value.AsInt()));
        break;
    case ScriptValueType::Float:
        lua_pushnumber(state, static_cast<lua_Number>(value.AsFloat()));
        break;
    case ScriptValueType::String:
        lua_pushlstring(state, value.AsString().data(), value.AsString().size());
        break;
    case ScriptValueType::Entity:
    case ScriptValueType::Component:
        lua_pushinteger(state, static_cast<lua_Integer>(value.AsUInt64()));
        break;
    case ScriptValueType::Void:
        lua_pushnil(state);
        break;
    }
}

void PushSelf(lua_State* state, const ScriptExecutionContext& context) {
    lua_createtable(state, 0, 3);
    lua_pushinteger(state, static_cast<lua_Integer>(context.Self().Id()));
    lua_setfield(state, -2, "entity");
    lua_pushinteger(state, static_cast<lua_Integer>(context.Asset().value));
    lua_setfield(state, -2, "asset");
    lua_pushstring(state, "Lua");
    lua_setfield(state, -2, "backend");
}

void PushEvent(lua_State* state, const ScriptEvent& event) {
    lua_createtable(state, 0, 4);
    lua_pushlstring(state, event.name.data(), event.name.size());
    lua_setfield(state, -2, "name");
    lua_pushinteger(state, static_cast<lua_Integer>(event.sender.Id()));
    lua_setfield(state, -2, "sender");
    lua_pushinteger(state, static_cast<lua_Integer>(event.senderAsset.value));
    lua_setfield(state, -2, "senderAsset");
    lua_createtable(state, 0, static_cast<int>(event.arguments.size()));
    for (const ScriptEventArgument& argument : event.arguments) {
        PushScriptValue(state, argument.value);
        lua_setfield(state, -2, argument.name.c_str());
    }
    lua_setfield(state, -2, "args");
}

void AttachEmit(lua_State* state, int environmentIndex, ScriptExecutionContext& context) {
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, &LuaEmit, 1);
    lua_setfield(state, environmentIndex, "Emit");
}

void CreateEnvironment(lua_State* state) {
    lua_newtable(state);
    const int environmentIndex = lua_gettop(state);

    lua_newtable(state);
    lua_pushglobaltable(state);
    lua_setfield(state, -2, "__index");
    lua_setmetatable(state, environmentIndex);
}

} // namespace

PucLuaScriptRuntime::PucLuaScriptRuntime()
    : state_(luaL_newstate()) {
    if (state_ != nullptr) {
        OpenSafeLibraries(state_);
    }
}

PucLuaScriptRuntime::~PucLuaScriptRuntime() {
    if (state_ != nullptr) {
        lua_close(state_);
        state_ = nullptr;
    }
}

PucLuaLoadResult PucLuaScriptRuntime::LoadScript(kb::assets::AssetId assetId, std::string_view source, std::string_view chunkName) {
    if (state_ == nullptr) {
        return PucLuaLoadResult{.error = "lua state could not be created"};
    }
    if (!assetId.IsValid()) {
        return PucLuaLoadResult{.error = "lua script asset id is invalid"};
    }

    LuaStackScope stack{state_};
    CreateEnvironment(state_);
    const int environmentIndex = lua_gettop(state_);

    const std::string chunk = chunkName.empty() ? std::string{"lua-script-"} + std::to_string(assetId.value) : std::string{chunkName};
    if (luaL_loadbufferx(state_, source.data(), source.size(), chunk.c_str(), "t") != LUA_OK) {
        return PucLuaLoadResult{.error = ErrorFromTop(state_)};
    }

    lua_pushvalue(state_, environmentIndex);
    static_cast<void>(lua_setupvalue(state_, -2, 1));
    if (lua_pcall(state_, 0, 0, 0) != LUA_OK) {
        return PucLuaLoadResult{.error = ErrorFromTop(state_)};
    }

    lua_pushvalue(state_, environmentIndex);
    const int environmentRef = luaL_ref(state_, LUA_REGISTRYINDEX);
    UnloadScript(assetId);
    environments_[assetId.value] = environmentRef;
    return PucLuaLoadResult{.succeeded = true};
}

void PucLuaScriptRuntime::UnloadScript(kb::assets::AssetId assetId) noexcept {
    if (state_ == nullptr) {
        return;
    }
    const auto iter = environments_.find(assetId.value);
    if (iter == environments_.end()) {
        return;
    }
    luaL_unref(state_, LUA_REGISTRYINDEX, iter->second);
    environments_.erase(iter);
}

void PucLuaScriptRuntime::Clear() noexcept {
    if (state_ != nullptr) {
        for (const auto& [assetId, environmentRef] : environments_) {
            static_cast<void>(assetId);
            luaL_unref(state_, LUA_REGISTRYINDEX, environmentRef);
        }
    }
    environments_.clear();
}

bool PucLuaScriptRuntime::HasScript(kb::assets::AssetId assetId) const noexcept {
    return environments_.contains(assetId.value);
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
    const kb::assets::AssetId assetId{behaviour.behaviourAssetId};
    const int environmentRef = FindScriptEnvironment(assetId);
    if (environmentRef == kNoReference) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = "lua script is not loaded",
        });
        return result;
    }

    LuaStackScope stack{state_};
    lua_rawgeti(state_, LUA_REGISTRYINDEX, environmentRef);
    const int environmentIndex = lua_gettop(state_);
    AttachEmit(state_, environmentIndex, context);
    lua_getfield(state_, environmentIndex, std::string{functionName}.c_str());
    if (lua_isnil(state_, -1) != 0) {
        return result;
    }
    if (lua_isfunction(state_, -1) == 0) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = "lua script entry is not a function",
        });
        return result;
    }

    PushSelf(state_, context);
    int argCount = 1;
    if (event == nullptr) {
        lua_pushnumber(state_, static_cast<lua_Number>(context.DeltaSeconds()));
        ++argCount;
    } else {
        PushEvent(state_, *event);
        ++argCount;
    }

    if (lua_pcall(state_, argCount, 0, 0) != LUA_OK) {
        result.diagnostics.push_back(ScriptDiagnostic{
            .entity = context.Self(),
            .assetId = assetId,
            .backend = behaviour.backend,
            .message = ErrorFromTop(state_),
        });
        return result;
    }

    result.executed = true;
    return result;
}

int PucLuaScriptRuntime::FindScriptEnvironment(kb::assets::AssetId assetId) const noexcept {
    const auto iter = environments_.find(assetId.value);
    return iter == environments_.end() ? kNoReference : iter->second;
}

} // namespace kb::script
