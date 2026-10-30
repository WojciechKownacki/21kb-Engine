#include "script/lua/PucLuaStateGraph.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::script {
namespace {
constexpr std::size_t kMaximumNodes = 65536U;
constexpr std::size_t kMaximumEntries = 262144U;
constexpr std::size_t kMaximumPayloadBytes = 16U * 1024U * 1024U;
constexpr unsigned kMaximumDepth = 128U;

// Trivial values keep Lua's protected allocation-failure jump from skipping
// destructors of C++ temporaries. All containers live in State outside the call.
enum class ValueKind { Nil, Boolean, Integer, Number, Pointer, Object };
struct Value {
    ValueKind kind = ValueKind::Nil;
    lua_Integer integer = 0;
    lua_Number number = 0;
    void* pointer = nullptr;
    std::size_t object = 0U;
};
struct Node {
    int type = LUA_TNIL;
    int reference = LUA_NOREF;
    std::vector<std::pair<Value, Value>> entries;
    std::vector<Value> upvalues;
    std::vector<const void*> upvalueIds;
    Value metatable;
    std::vector<std::byte> bytes;
};
}

struct PucLuaStateGraph::State {
    std::weak_ptr<PucLuaStateLifetime> lifetime;
    lua_State* vm = nullptr;
    std::vector<Node> nodes;
    std::unordered_map<const void*, std::size_t> identities;
    std::span<const int> roots;
    std::size_t payloadBytes = 0U;
    std::size_t entryCount = 0U;
    const void* randomState = nullptr;
    std::size_t randomBytes = 0U;
    std::string failure;
    bool captured = false;

    ~State() {
        const auto owner = lifetime.lock();
        if (owner && vm != nullptr && owner->state == vm) {
            for (const auto& node : nodes) luaL_unref(vm, LUA_REGISTRYINDEX, node.reference);
        }
    }

    void Count(std::size_t bytes) {
        if (bytes > kMaximumPayloadBytes - payloadBytes) throw std::runtime_error("Lua snapshot exceeds its 16 MiB payload limit");
        payloadBytes += bytes;
    }

    void FindRandomState() {
        lua_getglobal(vm, "math");
        if (lua_istable(vm, -1)) {
            lua_getfield(vm, -1, "random");
            if (lua_iscfunction(vm, -1) && lua_getupvalue(vm, -1, 1) != nullptr) {
                if (lua_type(vm, -1) == LUA_TUSERDATA) {
                    // This is the standard library's private POD RNG state, not arbitrary userdata.
                    randomState = lua_touserdata(vm, -1);
                    randomBytes = lua_rawlen(vm, -1);
                }
                lua_pop(vm, 1);
            }
            lua_pop(vm, 1);
        }
        lua_pop(vm, 1);
    }

    Value ReadValue(int index, unsigned depth) {
        if (depth > kMaximumDepth || lua_checkstack(vm, 8) == 0) throw std::runtime_error("Lua snapshot nesting exceeds its limit");
        const int type = lua_type(vm, index);
        switch (type) {
        case LUA_TNIL: return {};
        case LUA_TBOOLEAN: return {.kind = ValueKind::Boolean, .integer = lua_toboolean(vm, index)};
        case LUA_TNUMBER:
            if (lua_isinteger(vm, index)) return {.kind = ValueKind::Integer, .integer = lua_tointeger(vm, index)};
            return {.kind = ValueKind::Number, .number = lua_tonumber(vm, index)};
        case LUA_TLIGHTUSERDATA: return {.kind = ValueKind::Pointer, .pointer = lua_touserdata(vm, index)};
        case LUA_TSTRING:
        case LUA_TTABLE:
        case LUA_TFUNCTION:
        case LUA_TUSERDATA: break;
        default: throw std::runtime_error("Lua snapshot contains an unsupported thread or value type");
        }
        index = lua_absindex(vm, index);
        const void* identity = type == LUA_TSTRING ? static_cast<const void*>(lua_tostring(vm, index)) : lua_topointer(vm, index);
        const auto found = identities.find(identity);
        if (found != identities.end()) return {.kind = ValueKind::Object, .object = found->second};
        if (nodes.size() == kMaximumNodes) throw std::runtime_error("Lua snapshot contains too many objects");
        if (type == LUA_TUSERDATA && (lua_touserdata(vm, index) != randomState || lua_rawlen(vm, index) != randomBytes)) {
            throw std::runtime_error("Lua snapshot cannot copy opaque userdata without a state codec");
        }
        const std::size_t object = nodes.size();
        nodes.emplace_back();
        nodes[object].type = type;
        lua_pushvalue(vm, index);
        nodes[object].reference = luaL_ref(vm, LUA_REGISTRYINDEX);
        identities.emplace(identity, object);
        Count(sizeof(Value));
        if (type == LUA_TSTRING || type == LUA_TUSERDATA) {
            const std::size_t size = lua_rawlen(vm, index);
            Count(size);
            const auto* bytes = type == LUA_TSTRING ? reinterpret_cast<const std::byte*>(lua_tostring(vm, index))
                : static_cast<const std::byte*>(lua_touserdata(vm, index));
            nodes[object].bytes.assign(bytes, bytes + size);
        } else if (type == LUA_TTABLE) {
            lua_pushnil(vm);
            while (lua_next(vm, index)) {
                if (++entryCount > kMaximumEntries) throw std::runtime_error("Lua snapshot contains too many table entries");
                const Value key = ReadValue(-2, depth + 1U);
                const Value value = ReadValue(-1, depth + 1U);
                nodes[object].entries.emplace_back(key, value);
                Count(2U * sizeof(Value));
                lua_pop(vm, 1);
            }
            if (lua_getmetatable(vm, index)) {
                const Value value = ReadValue(-1, depth + 1U);
                nodes[object].metatable = value;
                lua_pop(vm, 1);
            }
        } else {
            for (int upvalue = 1; lua_getupvalue(vm, index, upvalue) != nullptr; ++upvalue) {
                const void* id = lua_upvalueid(vm, index, upvalue);
                const Value value = ReadValue(-1, depth + 1U);
                nodes[object].upvalueIds.push_back(id);
                nodes[object].upvalues.push_back(value);
                Count(sizeof(Value));
                lua_pop(vm, 1);
            }
        }
        return {.kind = ValueKind::Object, .object = object};
    }

    void Push(const Value& value) {
        switch (value.kind) {
        case ValueKind::Nil: lua_pushnil(vm); break;
        case ValueKind::Boolean: lua_pushboolean(vm, value.integer != 0); break;
        case ValueKind::Integer: lua_pushinteger(vm, value.integer); break;
        case ValueKind::Number: lua_pushnumber(vm, value.number); break;
        case ValueKind::Pointer: lua_pushlightuserdata(vm, value.pointer); break;
        case ValueKind::Object: lua_rawgeti(vm, LUA_REGISTRYINDEX, nodes[value.object].reference); break;
        }
    }

    void Apply() {
        // Clear every table first so cycles and shared table identities remain intact.
        for (const auto& node : nodes) {
            if (node.type != LUA_TTABLE) continue;
            lua_rawgeti(vm, LUA_REGISTRYINDEX, node.reference);
            const int table = lua_gettop(vm);
            lua_pushnil(vm);
            while (lua_next(vm, table)) {
                lua_pop(vm, 1);
                lua_pushvalue(vm, -1);
                lua_pushnil(vm);
                lua_rawset(vm, table);
            }
            lua_pop(vm, 1);
        }
        for (const auto& node : nodes) {
            lua_rawgeti(vm, LUA_REGISTRYINDEX, node.reference);
            const int index = lua_gettop(vm);
            if (node.type == LUA_TTABLE) {
                for (const auto& entry : node.entries) { Push(entry.first); Push(entry.second); lua_rawset(vm, index); }
                Push(node.metatable);
                lua_setmetatable(vm, index);
            } else if (node.type == LUA_TFUNCTION) {
                for (std::size_t upvalue = 0U; upvalue < node.upvalues.size(); ++upvalue) {
                    Push(node.upvalues[upvalue]);
                    static_cast<void>(lua_setupvalue(vm, index, static_cast<int>(upvalue + 1U)));
                }
            } else if (node.type == LUA_TUSERDATA) {
                std::memcpy(lua_touserdata(vm, index), node.bytes.data(), node.bytes.size());
            }
            lua_pop(vm, 1);
        }
    }

    static int CaptureCall(lua_State* callState) {
        auto* self = static_cast<State*>(lua_touserdata(callState, lua_upvalueindex(1)));
        try {
            self->FindRandomState();
            for (int root : self->roots) {
                lua_rawgeti(callState, LUA_REGISTRYINDEX, root);
                static_cast<void>(self->ReadValue(-1, 0U));
                lua_pop(callState, 1);
            }
            return 0;
        } catch (const std::exception& exception) { self->failure = exception.what(); }
        lua_pushlstring(callState, self->failure.data(), self->failure.size());
        return lua_error(callState);
    }

    static int RestoreCall(lua_State* callState) {
        auto* self = static_cast<State*>(lua_touserdata(callState, lua_upvalueindex(1)));
        try { self->Apply(); return 0; }
        catch (const std::exception& exception) { self->failure = exception.what(); }
        lua_pushlstring(callState, self->failure.data(), self->failure.size());
        return lua_error(callState);
    }

    bool Call(lua_CFunction function, std::string& error) {
        const auto owner = lifetime.lock();
        if (!owner || vm == nullptr || owner->state != vm) { error = "Lua snapshot belongs to a destroyed VM"; return false; }
        PucLuaStackGuard stack{vm};
        const bool collecting = lua_gc(vm, LUA_GCISRUNNING) != 0;
        lua_gc(vm, LUA_GCSTOP);
        lua_pushlightuserdata(vm, this);
        lua_pushcclosure(vm, function, 1);
        int result;
        { PucLuaMemoryLimitScope memory{vm}; result = lua_pcall(vm, 0, 0, 0); }
        if (result != LUA_OK) error = PucLuaErrorReporter::ErrorFromTop(vm);
        else error.clear();
        if (collecting) lua_gc(vm, LUA_GCRESTART);
        return result == LUA_OK;
    }
};

PucLuaStateGraph::PucLuaStateGraph(const std::shared_ptr<PucLuaStateLifetime>& lifetime)
    : state_(std::make_unique<State>()) {
    state_->lifetime = lifetime;
    state_->vm = lifetime->state;
}
PucLuaStateGraph::~PucLuaStateGraph() = default;

bool PucLuaStateGraph::Capture(std::span<const int> roots, std::string& error) {
    if (!state_->nodes.empty()) { error = "Lua snapshot graph was already captured"; return false; }
    state_->roots = roots;
    const bool result = state_->Call(&State::CaptureCall, error);
    state_->captured = result;
    state_->roots = {};
    return result;
}

bool PucLuaStateGraph::Validate(std::string& error) const {
    const auto owner = state_->lifetime.lock();
    if (!owner || state_->vm == nullptr || owner->state != state_->vm) { error = "Lua snapshot belongs to a destroyed VM"; return false; }
    if (!state_->captured) { error = "Lua snapshot capture did not complete"; return false; }
    PucLuaStackGuard stack{state_->vm};
    for (const auto& node : state_->nodes) {
        lua_rawgeti(state_->vm, LUA_REGISTRYINDEX, node.reference);
        if (lua_type(state_->vm, -1) != node.type) { error = "Lua snapshot object identity changed"; return false; }
        if (node.type == LUA_TFUNCTION) {
            for (std::size_t i = 0; i < node.upvalueIds.size(); ++i) {
                if (lua_upvalueid(state_->vm, -1, static_cast<int>(i + 1U)) != node.upvalueIds[i]) {
                    error = "Lua snapshot closure upvalue identity changed"; return false;
                }
            }
        }
        lua_pop(state_->vm, 1);
    }
    error.clear();
    return true;
}

bool PucLuaStateGraph::Restore(std::string& error) {
    return Validate(error) && state_->Call(&State::RestoreCall, error);
}
std::size_t PucLuaStateGraph::PayloadBytes() const noexcept { return state_->payloadBytes; }

} // namespace kb::script
