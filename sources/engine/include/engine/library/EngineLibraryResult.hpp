#pragma once

#include "engine/library/EngineLibraryError.hpp"

#include <optional>
#include <utility>

namespace kb::library {

// The uniform return shape for a kb::library operation that can fail:
// either the value it produced, or the ScriptError naming why it didn't.
// kb::library does not introduce a general Result<T,E> template — every
// failure in this namespace is a ScriptError, matching how the rest of the
// engine settles on one error shape per subsystem (ScriptFunctionCallResult,
// ScriptBackendExecutionResult, EngineLibraryModuleResult) rather than a
// generic, error-type-parameterized Result/Expected.
template <typename T>
class Result final {
public:
    [[nodiscard]] static Result Ok(T value) {
        Result result;
        result.value_.emplace(std::move(value));
        return result;
    }

    [[nodiscard]] static Result Fail(ScriptError error) {
        Result result;
        result.error_.emplace(std::move(error));
        return result;
    }

    [[nodiscard]] bool Succeeded() const noexcept { return value_.has_value(); }

    // Precondition: Succeeded(). Throws std::bad_optional_access otherwise
    // (via std::optional::value()) rather than reading garbage, so a
    // caller bug surfaces immediately instead of silently misbehaving.
    [[nodiscard]] const T& Value() const& { return value_.value(); }
    [[nodiscard]] T&& Value() && { return std::move(value_.value()); }

    // Precondition: !Succeeded(). Same throw-on-misuse contract as Value().
    [[nodiscard]] const ScriptError& Error() const& { return error_.value(); }

private:
    Result() = default;

    std::optional<T> value_;
    std::optional<ScriptError> error_;
};

} // namespace kb::library
