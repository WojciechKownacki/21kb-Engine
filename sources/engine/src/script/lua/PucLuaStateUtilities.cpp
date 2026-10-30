#include "script/lua/PucLuaStateUtilities.hpp"
#include "script/lua/PucLuaMemoryArena.hpp"
#include "script/lua/PucLuaRuntimeApi.hpp"

extern "C" {
#include <lauxlib.h>
}

#include <cstdlib>

namespace kb::script {

void* PucLuaMemoryBudget::Allocate(void* userData, void* block, std::size_t oldSize, std::size_t newSize) noexcept {
    auto& budget = *static_cast<PucLuaMemoryBudget*>(userData);
    static_cast<void>(oldSize); // The arena owns logical sizes and retained capacities.
    try {
        if (!budget.arena) budget.arena = std::make_shared<PucLuaMemoryArena>();
        void* resized = budget.arena->Resize(block, newSize, budget.limitBytes,
            budget.protectedDepth != 0U, budget.limitReached);
        budget.usedBytes = budget.arena->LiveBytes();
        return resized;
    } catch (const std::bad_alloc&) { return nullptr; }
}

PucLuaExecutionContextScope::PucLuaExecutionContextScope(lua_State* state, ScriptExecutionContext& context) noexcept
    : slot_(PucLuaRuntimeApi::ContextSlot(state)) {
    if (slot_ != nullptr) { previous_ = slot_->activeContext; slot_->activeContext = &context; }
}
PucLuaExecutionContextScope::~PucLuaExecutionContextScope() {
    if (slot_ != nullptr) slot_->activeContext = previous_;
}

PucLuaMemoryLimitScope::PucLuaMemoryLimitScope(lua_State* state) noexcept {
    void* userData = nullptr;
    if (lua_getallocf(state, &userData) == &PucLuaMemoryBudget::Allocate && userData != nullptr) {
        budget_ = static_cast<PucLuaMemoryBudget*>(userData);
        if (budget_->protectedDepth++ == 0U) {
            budget_->limitReached = false;
        }
    }
}

PucLuaMemoryLimitScope::~PucLuaMemoryLimitScope() {
    if (budget_ != nullptr) {
        --budget_->protectedDepth;
    }
}

PucLuaStackGuard::PucLuaStackGuard(lua_State* state) noexcept
    : state_(state)
    , top_(lua_gettop(state)) {}

PucLuaStackGuard::~PucLuaStackGuard() {
    lua_settop(state_, top_);
}

std::string PucLuaErrorReporter::ErrorFromTop(lua_State* state) {
    const char* error = lua_tostring(state, -1);
    std::string message = error == nullptr ? std::string{ "lua error" } : std::string{ error };
    void* userData = nullptr;
    if (lua_getallocf(state, &userData) == &PucLuaMemoryBudget::Allocate && userData != nullptr) {
        auto& budget = *static_cast<PucLuaMemoryBudget*>(userData);
        if (budget.limitReached && message.starts_with("not enough memory")) {
            budget.limitReached = false;
            message = "lua script memory limit of " + std::to_string(budget.limitBytes) + " bytes exceeded: " + message;
        }
    }
    return message;
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
