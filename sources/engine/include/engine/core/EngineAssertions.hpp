#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::core {

enum class AssertionPolicy { Development, Release };
enum class AssertionKind { Assert, Require, SoftFail };

struct AssertionResult {
    bool passed = false;
    bool fatal = false;
    AssertionKind kind = AssertionKind::Assert;
    std::string message;
    std::vector<std::string> scriptStackTrace;
};

[[nodiscard]] inline AssertionResult EvaluateAssertion(AssertionKind kind,
    bool condition, AssertionPolicy policy, std::string_view message,
    std::vector<std::string> scriptStackTrace = {}) {
    const bool fatal = !condition && (kind == AssertionKind::Require ||
        (kind == AssertionKind::Assert && policy == AssertionPolicy::Development));
    return { .passed = condition, .fatal = fatal, .kind = kind,
        .message = std::string{ message },
        .scriptStackTrace = std::move(scriptStackTrace) };
}

[[nodiscard]] inline AssertionResult Assert(bool condition,
    AssertionPolicy policy, std::string_view message,
    std::vector<std::string> scriptStackTrace = {}) {
    return EvaluateAssertion(AssertionKind::Assert, condition, policy, message,
        std::move(scriptStackTrace));
}

[[nodiscard]] inline AssertionResult Require(bool condition,
    AssertionPolicy policy, std::string_view message,
    std::vector<std::string> scriptStackTrace = {}) {
    return EvaluateAssertion(AssertionKind::Require, condition, policy, message,
        std::move(scriptStackTrace));
}

[[nodiscard]] inline AssertionResult SoftFail(bool condition,
    AssertionPolicy policy, std::string_view message,
    std::vector<std::string> scriptStackTrace = {}) {
    return EvaluateAssertion(AssertionKind::SoftFail, condition, policy, message,
        std::move(scriptStackTrace));
}

[[nodiscard]] inline AssertionResult EvaluateAssertion(bool condition,
    AssertionPolicy policy, std::string_view message) {
    return Assert(condition, policy, message);
}

} // namespace kb::core
