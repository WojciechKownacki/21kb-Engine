#pragma once

extern "C" {
#include <lua.h>
}

#include <string>

namespace kb::script {

class PucLuaStackGuard final {
public:
    explicit PucLuaStackGuard(lua_State* state) noexcept;
    ~PucLuaStackGuard();

    PucLuaStackGuard(const PucLuaStackGuard&) = delete;
    PucLuaStackGuard& operator=(const PucLuaStackGuard&) = delete;

private:
    lua_State* state_ = nullptr;
    int top_ = 0;
};

class PucLuaErrorReporter final {
public:
    [[nodiscard]] static std::string ErrorFromTop(lua_State* state);
    // lua_resume does not accept an error-handler function like lua_pcall.
    // Build the traceback directly from the suspended coroutine while its
    // failing call frames are still available.
    [[nodiscard]] static std::string ErrorWithTracebackFromTop(lua_State* state);
    [[nodiscard]] static std::string ChunkFromDebug(lua_Debug& debug);
    static int Traceback(lua_State* state);
};

} // namespace kb::script
