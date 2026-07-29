#pragma once

#include <cstddef>

namespace kb::core {
enum class BudgetExceededPolicy { Suspend, Fail };
class ExecutionBudget final { public: explicit ExecutionBudget(std::size_t limit, BudgetExceededPolicy policy=BudgetExceededPolicy::Fail):remaining_(limit),policy_(policy){} [[nodiscard]] bool Consume() noexcept { if(remaining_==0U)return false;--remaining_;return true;} [[nodiscard]] BudgetExceededPolicy Policy() const noexcept{return policy_;} private:std::size_t remaining_;BudgetExceededPolicy policy_;};
} // namespace kb::core
