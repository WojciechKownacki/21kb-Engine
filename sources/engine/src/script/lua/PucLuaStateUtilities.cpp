#include "script/lua/PucLuaStateUtilities.hpp"

extern "C" {
#include <lauxlib.h>
}

namespace kb::script {

PucLuaStackGuard::PucLuaStackGuard(lua_State* state) noexcept
    : state_(state)
    , top_(lua_gettop(state)) {}

PucLuaStackGuard::~PucLuaStackGuard() {
    lua_settop(state_, top_);
}

std::string PucLuaErrorReporter::ErrorFromTop(lua_State* state) {
    const char* error = lua_tostring(state, -1);
    return error == nullptr ? std::string{ "lua error" } : std::string{ error };
}

std::string PucLuaErrorReporter::ErrorWithTracebackFromTop(lua_State* state) {
    const char* message = lua_tostring(state, -1);
    if (message == nullptr) {
        message = "lua error";
    }
    luaL_traceback(state, state, message, 1);
    return ErrorFromTop(state);
}

std::string PucLuaErrorReporter::ChunkFromDebug(lua_Debug& debug) {
    if (debug.source == nullptr) {
        return {};
    }
    std::string source{ debug.source };
    if (!source.empty() && source.front() == '@') {
        source.erase(source.begin());
    }
    return source;
}

int PucLuaErrorReporter::Traceback(lua_State* state) {
    const char* message = lua_tostring(state, 1);
    if (message == nullptr) {
        message = "lua error";
    }
    luaL_traceback(state, state, message, 1);
    return 1;
}

} // namespace kb::script
