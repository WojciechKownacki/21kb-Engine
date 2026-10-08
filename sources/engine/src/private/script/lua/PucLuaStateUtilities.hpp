#pragma once

extern "C" {
#include <lua.h>
}

#include <cstddef>
#include <cstdint>
#include <string>

namespace kb::script {

// Allocation accounting for one Lua state, installed as its lua_Alloc. Growth
// past limitBytes (zero: unlimited) is refused only while protectedDepth is
// non-zero, i.e. while script code runs inside lua_pcall/lua_resume, where the
// refusal surfaces as a catchable LUA_ERRMEM. Host-side stack set-up between
// protected calls is never refused: Lua turns an allocation failure outside a
// protected call into a panic that ends the process.
struct PucLuaMemoryBudget final {
    std::size_t usedBytes = 0U;
    std::size_t limitBytes = 0U;
    std::uint32_t protectedDepth = 0U;
    bool limitReached = false;

    static void* Allocate(void* userData, void* block, std::size_t oldSize, std::size_t newSize) noexcept;
};

// Enforces the state's memory budget for the protected call made while it is
// alive. Nests, so a script calling back into a protected call stays bounded.
class PucLuaMemoryLimitScope final {
public:
    explicit PucLuaMemoryLimitScope(lua_State* state) noexcept;
    ~PucLuaMemoryLimitScope();

    PucLuaMemoryLimitScope(const PucLuaMemoryLimitScope&) = delete;
    PucLuaMemoryLimitScope& operator=(const PucLuaMemoryLimitScope&) = delete;

private:
    PucLuaMemoryBudget* budget_ = nullptr;
};

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
    // A failed protected call's message; a refusal by the memory budget is
    // named as such instead of Lua's bare "not enough memory".
    [[nodiscard]] static std::string ErrorFromTop(lua_State* state);
    // lua_resume does not accept an error-handler function like lua_pcall.
    // Build the traceback directly from the suspended coroutine while its
    // failing call frames are still available.
    [[nodiscard]] static std::string ErrorWithTracebackFromTop(lua_State* state);
    [[nodiscard]] static std::string ChunkFromDebug(lua_Debug& debug);
    static int Traceback(lua_State* state);
};

} // namespace kb::script
