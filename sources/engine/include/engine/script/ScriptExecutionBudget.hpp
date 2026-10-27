#pragma once

#include "engine/core/ExecutionBudget.hpp"

#include <cstddef>

namespace kb::script {

struct ScriptExecutionBudgetSettings {
    // Bound script loading and behaviour calls even when a host uses defaults.
    // Trusted hosts may explicitly opt out with zero.
    std::size_t luaInstructionsPerBehaviour = 1'000'000U;
    std::size_t visualGraphStepsPerBehaviour = 4'096U;
    kb::core::BudgetExceededPolicy policy = kb::core::BudgetExceededPolicy::Fail;
    // Bytes one Lua runtime (all scripts a host runs) may allocate while a
    // script executes. An allocation past it fails as a Lua memory error that
    // is reported as that script's error; zero means unlimited.
    std::size_t luaMemoryBytes = std::size_t{ 256U } << 20U;
};

} // namespace kb::script
