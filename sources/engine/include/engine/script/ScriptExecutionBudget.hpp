#pragma once

#include "engine/core/ExecutionBudget.hpp"

#include <cstddef>

namespace kb::script {

struct ScriptExecutionBudgetSettings {
    // Zero preserves the existing uninstrumented Lua execution path. A host
    // enables the hard instruction cap deliberately for the target build.
    std::size_t luaInstructionsPerBehaviour = 0U;
    std::size_t visualGraphStepsPerBehaviour = 4'096U;
    kb::core::BudgetExceededPolicy policy = kb::core::BudgetExceededPolicy::Fail;
};

} // namespace kb::script
