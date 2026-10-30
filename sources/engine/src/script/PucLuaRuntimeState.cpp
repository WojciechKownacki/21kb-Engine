#include "engine/script/PucLuaScriptRuntime.hpp"
#include "script/lua/PucLuaStateGraph.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <utility>

namespace kb::script {

struct PucLuaScriptRuntime::RuntimeSnapshot final : kb::core::RuntimeStateSnapshot {
    explicit RuntimeSnapshot(const std::shared_ptr<PucLuaStateLifetime>& owner) : lifetime(owner), graph(owner) {}
    std::weak_ptr<PucLuaStateLifetime> lifetime;
    mutable PucLuaStateGraph graph;
    std::uint64_t generation = 0U;
    decltype(scripts_) scripts;
    decltype(modules_) modules;
    decltype(exposedVariables_) exposedVariables;
    decltype(instanceVariables_) instanceVariables;
    std::size_t variableBytes = 0U;
    [[nodiscard]] std::size_t PayloadBytes() const noexcept override { return graph.PayloadBytes() + variableBytes; }
};

std::unique_ptr<kb::core::RuntimeStateSnapshot> PucLuaScriptRuntime::CaptureRuntimeState(std::string& error) {
    error.clear();
    if (state_ == nullptr) { error = "Lua state is unavailable"; return {}; }
    if (SuspendedCoroutineCount() != 0U || debugPause_.has_value()) {
        error = "Lua snapshot cannot capture suspended coroutine instruction state"; return {};
    }
    if (!eventSubscriptionHandles_.empty()) {
        error = "Lua event subscription state requires a coordinated script-host snapshot"; return {};
    }
    auto snapshot = std::make_unique<RuntimeSnapshot>(stateLifetime_);
    snapshot->generation = generation_;
    snapshot->scripts = scripts_;
    snapshot->modules = modules_;
    snapshot->exposedVariables = exposedVariables_;
    snapshot->instanceVariables = instanceVariables_;
    std::vector<int> roots;
    for (const auto& [id, record] : scripts_) { static_cast<void>(id); roots.push_back(record.environmentRef); }
    for (const auto& [name, record] : modules_) {
        static_cast<void>(name);
        if (record.loading) { error = "Lua module is still loading: " + name; return {}; }
        if (record.valueRef >= 0) roots.push_back(record.valueRef);
    }
    if (!snapshot->graph.Capture(roots, error)) return {};
    for (const auto& [key, variables] : instanceVariables_) {
        static_cast<void>(key);
        for (const auto& variable : variables) snapshot->variableBytes += sizeof(variable) + variable.name.size() + variable.value.AsString().size();
    }
    return snapshot;
}

bool PucLuaScriptRuntime::ValidateRuntimeState(const kb::core::RuntimeStateSnapshot& state, std::string& error) const {
    const auto* snapshot = dynamic_cast<const RuntimeSnapshot*>(&state);
    if (snapshot == nullptr || snapshot->lifetime.lock() != stateLifetime_ || state_ == nullptr) {
        error = "Lua snapshot belongs to a different VM lifetime"; return false;
    }
    if (snapshot->generation != generation_ || snapshot->scripts.size() != scripts_.size() || snapshot->modules.size() != modules_.size()) {
        error = "Lua script or module catalog changed since capture"; return false;
    }
    for (const auto& [id, record] : snapshot->scripts) {
        const auto found = scripts_.find(id);
        if (found == scripts_.end() || found->second.generation != record.generation || found->second.environmentRef != record.environmentRef) {
            error = "Lua script changed since capture: " + record.chunkName; return false;
        }
    }
    for (const auto& [name, record] : snapshot->modules) {
        const auto found = modules_.find(name);
        if (found == modules_.end() || found->second.generation != record.generation || found->second.loading) {
            error = "Lua module changed since capture: " + name; return false;
        }
    }
    if (!eventSubscriptionHandles_.empty()) {
        error = "Lua event subscription state requires a coordinated script-host restore"; return false;
    }
    return snapshot->graph.Validate(error);
}

bool PucLuaScriptRuntime::RestoreRuntimeState(const kb::core::RuntimeStateSnapshot& state, std::string& error) {
    if (!ValidateRuntimeState(state, error)) return false;
    const auto& snapshot = static_cast<const RuntimeSnapshot&>(state);
    while (!coroutineRefs_.empty()) ClearCoroutines(coroutineRefs_.begin()->first);
    if (!snapshot.graph.Restore(error)) return false;
    exposedVariables_ = snapshot.exposedVariables;
    instanceVariables_ = snapshot.instanceVariables;
    // A module first imported after capture returns to its not-yet-loaded state.
    for (auto& [name, module] : modules_) {
        const auto& saved = snapshot.modules.at(name);
        if (module.valueRef != saved.valueRef) {
            luaL_unref(state_, LUA_REGISTRYINDEX, module.valueRef);
            module.valueRef = saved.valueRef;
            module.loading = false;
        }
    }
    return true;
}

} // namespace kb::script
