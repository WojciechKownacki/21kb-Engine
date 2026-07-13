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
//
// LIB-061 ("expose Option<T>/Result<T,E> in languages that support them"):
// for native C++ — the one language in this engine with real sum-type
// support — this Result<T> (T-or-ScriptError) already IS that Result<T,E>,
// and std::optional<T> (used pervasively: ScriptSharedState::Get,
// EntityHandle::CheckError, etc.) already IS that Option<T>. Neither Lua
// nor Visual Graph can express a generic templated sum type across the
// script boundary — ScriptValue is a fixed, purely scalar tagged union
// (LIB-032/LIB-041) — so "expose them" for those two means an idiomatic
// per-language ADAPTER instead of the same generic type:
//   - Lua: `value, err = CallFunction(...)` — nil plus an error string on
//     failure, the real value (or a table, for 2+ outputs) on success.
//     Already implemented (PucLuaFunctionApi.cpp::LuaCallFunction) and
//     exercised end-to-end from real Lua script text by
//     RunLuaCallFunctionResultAdapterTest (ScriptRuntimeTests.cpp).
//   - Visual Graph: a "failed" exec output pin on CallNative nodes,
//     alongside the pre-existing "then" (success) pin — mirroring the
//     Branch node's "true"/"false" pair (VisualGraphNodeDefinitionRegistry.cpp,
//     VisualGraphCompiler.cpp, VisualGraphRuntimeExecutor::ExecuteNode).
//     A wired "failed" handler lets the graph react to a failed call
//     instead of the whole Tick halting at the point of failure; an
//     unwired one preserves the pre-LIB-061 fail-loud default. Tested by
//     RunVisualGraphCallNativeFailureBranchTest (ScriptRuntimeTests.cpp).
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
