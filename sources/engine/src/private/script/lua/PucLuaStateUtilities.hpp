#pragma once

extern "C" {
#include <lua.h>
}

#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>

namespace kb::script {

class PucLuaMemoryArena;
class ScriptExecutionContext;
// The object a `self` table belongs to: the entity and the script asset it runs.
struct PucLuaSelfKey {
    std::uint64_t entityId = 0U;
    std::uint64_t assetId = 0U;

    [[nodiscard]] bool operator==(const PucLuaSelfKey& other) const noexcept = default;
};

struct PucLuaSelfKeyHasher {
    [[nodiscard]] std::size_t operator()(PucLuaSelfKey key) const noexcept {
        std::uint64_t hash = key.entityId;
        hash ^= key.assetId + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
        return static_cast<std::size_t>(hash);
    }
};

struct PucLuaStateLifetime {
    lua_State* state = nullptr;
    ScriptExecutionContext* activeContext = nullptr;
    std::unordered_map<int, const void*> eventReferenceOwners;
    // Registry references to the `self` table of each object, kept for the life of the object.
    std::unordered_map<PucLuaSelfKey, int, PucLuaSelfKeyHasher> selfReferences;
    // The coroutine thread entries run on: one is kept anchored in the registry and handed to the next entry when
    // the last one finished, so a call does not go through the table of idle threads.
    lua_State* spareThread = nullptr;
    int spareThreadReference = -2; // LUA_NOREF
    bool spareThreadBusy = false;
    // The instruction budget's count hook stays on the kept thread between entries instead of being set and cleared
    // around each one.
    bool spareThreadHooked = false;
};

class PucLuaExecutionContextScope final {
public:
    PucLuaExecutionContextScope(lua_State* state, ScriptExecutionContext& context) noexcept;
    ~PucLuaExecutionContextScope();
private:
    PucLuaStateLifetime* slot_ = nullptr;
    ScriptExecutionContext* previous_ = nullptr;
};

// Allocation accounting for one Lua state, installed as its lua_Alloc. Growth
// past limitBytes (zero: unlimited) is refused only while protectedDepth is
// non-zero, i.e. while script code runs inside lua_pcall/lua_resume, where the
// refusal surfaces as a catchable LUA_ERRMEM. Host-side stack set-up between
// protected calls is never refused: Lua turns an allocation failure outside a
// protected call into a panic that ends the process.
struct PucLuaMemoryBudget final {
    std::size_t usedBytes = 0U;
    // Blocks the state asked its allocator for since it was created.
    std::uint64_t allocationCount = 0U;
    std::size_t limitBytes = 0U;
    std::uint32_t protectedDepth = 0U;
    bool limitReached = false;
    std::shared_ptr<PucLuaMemoryArena> arena;

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
