// The execution budget on Lua 5.4: the interpreter charges one unit for every loop iteration and call (see
// CMake/PatchLuaRunaway.cmake) and, when the allowance is spent, arms the debug hook on the running thread.
#include "script/lua/LuaVm.hpp"
#include "script/lua/PucLuaRunaway.hpp"

#include "engine/script/PucLuaScriptRuntime.hpp"
#include "script/lua/PucLuaDebugHook.hpp"

#include <algorithm>
#include <climits>

namespace kb::script {

struct PucLuaRunaway::Impl {
    lua_State* state = nullptr;
};

PucLuaRunaway::PucLuaRunaway(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
PucLuaRunaway::~PucLuaRunaway() = default;

std::unique_ptr<PucLuaRunaway> PucLuaRunaway::Create(lua_State* state, PucLuaScriptRuntime& /*runtime*/) {
    auto impl = std::make_unique<Impl>();
    impl->state = state;
    return std::unique_ptr<PucLuaRunaway>(new PucLuaRunaway(std::move(impl)));
}

void PucLuaRunaway::Arm(std::size_t limit) noexcept {
    lua_setrunaway(impl_->state, PucLuaDebugHook::HookFunction(), static_cast<int>(std::min<std::size_t>(limit, static_cast<std::size_t>(INT_MAX))));
}

void PucLuaRunaway::Disarm() noexcept {
    lua_setrunaway(impl_->state, nullptr, 0);
}

void PucLuaRunaway::Rearm(lua_State* /*state*/) noexcept {}

void PucLuaRunaway::OnCountHook(lua_State* state, PucLuaScriptRuntime& runtime, lua_Hook hook) {
    // The interpreter arms a count hook on the running thread when the allowance of loop iterations and calls given by
    // Arm is spent. One that fires while the allowance is not spent (armed in an earlier entry and not cleared) is
    // dropped.
    const int mask = lua_gethookmask(state);
    if (lua_getrunaway(state) > 0 || !runtime.HasActiveExecutionBudget()) {
        lua_sethook(state, hook, mask & ~LUA_MASKCOUNT, 0);
        return;
    }
    if (runtime.ExecutionBudgetPolicy() == kb::core::BudgetExceededPolicy::Suspend && lua_isyieldable(state) != 0) {
        lua_sethook(state, hook, mask & ~LUA_MASKCOUNT, 0);
        static_cast<void>(lua_yieldk(state, 0, 0, nullptr));
        return;
    }
    // A script can catch a hook error with pcall. The count hook stays armed with the spent allowance, so the very next
    // instruction stops it again and it cannot keep the host occupied by catching the error forever.
    luaL_error(state, "lua execution budget exceeded");
}

} // namespace kb::script
