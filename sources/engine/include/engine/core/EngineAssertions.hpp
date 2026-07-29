#pragma once
#include <string_view>
namespace kb::core { enum class AssertionPolicy { Development, Release }; struct AssertionResult { bool passed=false; bool fatal=false; std::string_view message; }; [[nodiscard]] constexpr AssertionResult EvaluateAssertion(bool condition, AssertionPolicy policy, std::string_view message) noexcept { return {condition,!condition&&policy==AssertionPolicy::Development,message}; } }
