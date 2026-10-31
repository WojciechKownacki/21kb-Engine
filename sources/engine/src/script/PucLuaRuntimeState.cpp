#include "engine/script/PucLuaScriptRuntime.hpp"
#include "script/lua/PucLuaMemoryArena.hpp"
#include "script/lua/PucLuaStateUtilities.hpp"

#include <algorithm>

namespace kb::script {

struct PucLuaScriptRuntime::RuntimeSnapshot final : kb::core::RuntimeStateSnapshot {
    std::weak_ptr<PucLuaStateLifetime> lifetime;
    std::unique_ptr<PucLuaHeapImage> heap;
    std::uint64_t generation = 0U;
    decltype(scripts_) scripts;
    decltype(modules_) modules;
    decltype(exposedVariables_) exposedVariables;
    decltype(instanceVariables_) instanceVariables;
    decltype(coroutineRefs_) coroutines;
    decltype(eventSubscriptionHandles_) subscriptions;
    decltype(PucLuaStateLifetime::eventReferenceOwners) eventReferenceOwners;
    decltype(debugSettings_) debugSettings;
    decltype(lastDebugPause_) lastDebugPause;
    decltype(debugStepMode_) stepMode = DebugStepMode::Run;
    lua_State* stepThread = nullptr;
    int stepDepth = 0;
    decltype(debugPause_) debugPause;
    lua_State* suspendThread = nullptr;
    int suspendDepth = 0;
    bool suspendAllowed = false;
    ScriptExecutionBudgetSettings budget;
    std::size_t remainingInstructions = 0U;
    bool budgetActive = false;
    [[nodiscard]] std::size_t PayloadBytes() const noexcept override { return heap->PayloadBytes(); }
};

std::size_t PucLuaScriptRuntime::LuaMemoryOwnedBytes() const noexcept {
    return memoryBudget_->arena ? memoryBudget_->arena->ResidentBytes() : 0U;
}

std::unique_ptr<kb::core::RuntimeStateSnapshot> PucLuaScriptRuntime::CaptureRuntimeState(std::string& error) {
    return CaptureRuntimeStateImpl(error, false);
}

std::unique_ptr<kb::core::RuntimeStateSnapshot> PucLuaScriptRuntime::CaptureRuntimeStateImpl(std::string& error, bool coordinatedHost) {
    error.clear();
    if (state_ == nullptr || !memoryBudget_->arena) { error = "Lua state is unavailable"; return {}; }
    if (memoryBudget_->protectedDepth != 0U || stateLifetime_->activeContext != nullptr) {
        error = "Lua checkpoints require a completed execution boundary"; return {};
    }
    if (!coordinatedHost && !eventSubscriptionHandles_.empty()) {
        error = "Lua event subscriptions require a coordinated script-host snapshot"; return {};
    }
    auto snapshot = std::make_unique<RuntimeSnapshot>();
    snapshot->lifetime = stateLifetime_;
    snapshot->generation = generation_;
    snapshot->scripts = scripts_;
    snapshot->modules = modules_;
    snapshot->exposedVariables = exposedVariables_;
    snapshot->instanceVariables = instanceVariables_;
    snapshot->coroutines = coroutineRefs_;
    snapshot->subscriptions = eventSubscriptionHandles_;
    snapshot->eventReferenceOwners = stateLifetime_->eventReferenceOwners;
    snapshot->debugSettings = debugSettings_;
    snapshot->lastDebugPause = lastDebugPause_;
    snapshot->stepMode = debugStepMode_;
    snapshot->stepThread = debugStepThread_;
    snapshot->stepDepth = debugStepDepth_;
    snapshot->debugPause = debugPause_;
    snapshot->suspendThread = debugSuspendThread_;
    snapshot->suspendDepth = debugSuspendDepth_;
    snapshot->suspendAllowed = debugSuspendAllowed_;
    snapshot->budget = executionBudgetSettings_;
    snapshot->remainingInstructions = remainingLuaInstructions_;
    snapshot->budgetActive = executionBudgetActive_;
    snapshot->heap = memoryBudget_->arena->Capture(memoryBudget_->limitBytes, error);
    if (!snapshot->heap) return {};
    return snapshot;
}

bool PucLuaScriptRuntime::ValidateRuntimeState(const kb::core::RuntimeStateSnapshot& state, std::string& error) const {
    return ValidateRuntimeStateImpl(state, error, false);
}

bool PucLuaScriptRuntime::ValidateRuntimeStateImpl(const kb::core::RuntimeStateSnapshot& state, std::string& error, bool coordinatedHost) const {
    const auto* snapshot = dynamic_cast<const RuntimeSnapshot*>(&state);
    if (snapshot == nullptr || snapshot->lifetime.lock() != stateLifetime_ || state_ == nullptr) {
        error = "Lua snapshot belongs to a different VM lifetime"; return false;
    }
    if (memoryBudget_->protectedDepth != 0U || stateLifetime_->activeContext != nullptr) {
        error = "Lua checkpoints require a completed execution boundary"; return false;
    }
    if (!coordinatedHost && !eventSubscriptionHandles_.empty()) {
        error = "Lua event subscriptions require a coordinated script-host restore"; return false;
    }
    error.clear();
    return true;
}

bool PucLuaScriptRuntime::RestoreRuntimeState(const kb::core::RuntimeStateSnapshot& state, std::string& error) {
    return RestoreRuntimeStateImpl(state, error, false);
}

bool PucLuaScriptRuntime::RestoreRuntimeStateImpl(const kb::core::RuntimeStateSnapshot& state, std::string& error, bool coordinatedHost) {
    if (!ValidateRuntimeStateImpl(state, error, coordinatedHost)) return false;
    const auto& snapshot = static_cast<const RuntimeSnapshot&>(state);
    if (!memoryBudget_->arena->Restore(*snapshot.heap, error)) return false;
    scripts_ = snapshot.scripts;
    modules_ = snapshot.modules;
    exposedVariables_ = snapshot.exposedVariables;
    instanceVariables_ = snapshot.instanceVariables;
    coroutineRefs_ = snapshot.coroutines;
    eventSubscriptionHandles_ = snapshot.subscriptions;
    stateLifetime_->eventReferenceOwners = snapshot.eventReferenceOwners;
    debugSettings_ = snapshot.debugSettings;
    lastDebugPause_ = snapshot.lastDebugPause;
    debugStepMode_ = snapshot.stepMode;
    debugStepThread_ = snapshot.stepThread;
    debugStepDepth_ = snapshot.stepDepth;
    debugPause_ = snapshot.debugPause;
    debugSuspendThread_ = snapshot.suspendThread;
    debugSuspendDepth_ = snapshot.suspendDepth;
    debugSuspendAllowed_ = snapshot.suspendAllowed;
    executionBudgetSettings_ = snapshot.budget;
    remainingLuaInstructions_ = snapshot.remainingInstructions;
    executionBudgetActive_ = snapshot.budgetActive;
    generation_ = std::max(generation_, snapshot.generation);
    memoryBudget_->usedBytes = memoryBudget_->arena->LiveBytes();
    memoryBudget_->limitBytes = snapshot.budget.luaMemoryBytes;
    memoryBudget_->limitReached = false;
    return true;
}

} // namespace kb::script
