#include "script/lua/api/PucLuaTaskApi.hpp"

#include "engine/script/ScriptApiCatalog.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <string>
#include <string_view>

namespace kb::script {
namespace {

constexpr std::string_view kTaskApiStorage = "__kb_internal_task_api";
constexpr std::string_view kTaskApiChunkName = "Engine21kbLibrary.Task";
constexpr std::string_view kTaskApiSource = R"lua(
local function callOrError(name, arguments, level)
    local value, callError = CallFunction(name, arguments)
    if value == nil then
        error(callError or (name .. " failed"), level)
    end
    return value
end

local function awaitTask(name, arguments)
    local task = callOrError(name, arguments, 3)
    while callOrError("Task.IsRunning", { task = task }, 3) do
        coroutine.yield()
    end
    return task
end

return {
    ["Task.WaitSeconds"] = function(seconds, owner)
        return awaitTask("Task.WaitSeconds", { seconds = seconds, owner = owner })
    end,
    ["Task.WaitFixedSteps"] = function(steps, owner)
        return awaitTask("Task.WaitFixedSteps", { steps = steps, owner = owner })
    end,
    ["Task.WaitEvent"] = function(eventName, owner)
        return awaitTask("Task.WaitEvent", { event = eventName, owner = owner })
    end,
    ["Task.WaitAsset"] = function(reference, owner)
        return awaitTask("Task.WaitAsset", { reference = reference, owner = owner })
    end,
    ["Task.WaitScene"] = function(sceneName, owner)
        return awaitTask("Task.WaitScene", { scene = sceneName, owner = owner })
    end,
    ["Task.IsRunning"] = function(task)
        return callOrError("Task.IsRunning", { task = task }, 2)
    end,
    ["Task.Cancel"] = function(task)
        return callOrError("Task.Cancel", { task = task }, 2)
    end,
}
)lua";

} // namespace

std::optional<std::string> PucLuaTaskApi::Attach(lua_State* state, int environmentIndex) {
    const int absoluteEnvironment = lua_absindex(state, environmentIndex);
    lua_getfield(state, absoluteEnvironment, kTaskApiStorage.data());
    if (lua_istable(state, -1) == 0) {
        lua_pop(state, 1);
        if (luaL_loadbufferx(
                state,
                kTaskApiSource.data(),
                kTaskApiSource.size(),
                kTaskApiChunkName.data(),
                "t") != LUA_OK) {
            return PucLuaErrorReporter::ErrorFromTop(state);
        }
        lua_pushvalue(state, absoluteEnvironment);
        static_cast<void>(lua_setupvalue(state, -2, 1));
        if (lua_pcall(state, 0, 1, 0) != LUA_OK) {
            return PucLuaErrorReporter::ErrorFromTop(state);
        }
        if (lua_istable(state, -1) == 0) {
            lua_pop(state, 1);
            return std::string{ "Engine21kbLibrary.Task initializer did not return a table" };
        }
        lua_pushvalue(state, -1);
        lua_setfield(state, absoluteEnvironment, kTaskApiStorage.data());
    }

    const int taskFactories = lua_absindex(state, -1);
    lua_newtable(state);
    const int taskTable = lua_gettop(state);
    for (const ScriptApiCatalogLuaBindingDefinition& binding : ScriptApiCatalog::LuaBindingDefinitions()) {
        if (binding.tableName != "Task") {
            continue;
        }
        lua_getfield(state, taskFactories, binding.functionName.data());
        if (lua_isfunction(state, -1) == 0) {
            lua_pop(state, 3);
            return std::string{ "Engine21kbLibrary.Task initializer is missing " } + std::string{ binding.functionName };
        }
        lua_setfield(state, taskTable, binding.luaName.data());
    }
    lua_setfield(state, absoluteEnvironment, "Task");
    lua_pop(state, 1);
    return std::nullopt;
}

} // namespace kb::script
