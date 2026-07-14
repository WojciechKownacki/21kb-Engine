#pragma once

#include "engine/library/EngineLibraryError.hpp"

#include <functional>
#include <optional>
#include <utility>

namespace kb::library {

// LIB-100: the state an AsyncResult<T> can be in — Running is the only
// non-terminal state; once it transitions to any of the other three it
// never transitions again (SetCompleted/SetFailed/Cancel all become no-ops,
// mirroring Timer.Cancel/Task.Cancel/World.Destroy's own idempotency
// precedent, LIB-067/095/097).
enum class AsyncState : std::uint8_t {
    Running,
    Completed,
    Failed,
    Cancelled,
};

// LIB-100: success, error, cancellation, and a completion callback for a
// native C++ async operation — the LAST task in this session's
// Coroutine/Task/yield/cancellation arc (LIB-093..100). Deliberately a
// STANDALONE value type, not built on top of kb::scene::SceneTasks: a Task's
// poll callback (kb::scene::TaskPollResult(float)) has no channel for a
// carried VALUE, only a tri-state result — AsyncResult<T> is the
// value-carrying counterpart, usable independently of whether anything ever
// polls it through SceneTasks (EngineLibraryTaskFactories.hpp's
// MakeTaskPollFromAsyncResult below is the optional bridge between the two,
// not a requirement).
//
// "Callback on the correct thread" (this task's own wording): this engine
// has NO threading infrastructure anywhere near scripts/assets/scenes —
// confirmed before implementing (no std::thread/std::async/thread pool
// wired to any of kb::script/kb::assets/kb::scene, the same finding LIB-098
// already made for asset/scene loading). Every AsyncResult<T> in this
// engine is therefore driven SYNCHRONOUSLY by native C++ code on whatever
// thread already owns the Scene — there is no other thread that could ever
// call SetCompleted/SetFailed/Cancel. "The correct thread" is satisfied by
// construction: this type never itself introduces a thread hop, so it never
// needs to marshal its callback across one. Real cross-thread completion
// would require actual background-thread infrastructure that does not
// exist anywhere in this engine today — a much larger, separately-scoped
// future change, not fabricated here (the same honesty LIB-098 already
// applied to asset/scene-load "yield").
template <typename T>
class AsyncResult final {
public:
    AsyncResult() = default;

    [[nodiscard]] AsyncState State() const noexcept { return state_; }
    [[nodiscard]] bool IsRunning() const noexcept { return state_ == AsyncState::Running; }
    [[nodiscard]] bool Succeeded() const noexcept { return state_ == AsyncState::Completed; }

    // Precondition: Succeeded(). Throws std::bad_optional_access otherwise
    // (via std::optional::value()), same throw-on-misuse contract as
    // kb::library::Result<T>::Value() (LIB-061).
    [[nodiscard]] const T& Value() const& { return value_.value(); }

    // Precondition: State() == AsyncState::Failed. Same throw-on-misuse
    // contract as Value() above.
    [[nodiscard]] const ScriptError& Error() const& { return error_.value(); }

    // Registers the completion callback — a SINGLE listener (registering
    // again replaces the previous one, the same single-callback convention
    // NativeScriptBackend::RegisterEvent already uses), called EXACTLY once
    // the moment this AsyncResult leaves Running, on whatever thread caused
    // that transition (see the class doc comment above). If already in a
    // terminal state when registered, the callback fires immediately,
    // synchronously, before this call returns — a late listener never
    // misses the result.
    void OnComplete(std::function<void(const AsyncResult&)> callback) {
        callback_ = std::move(callback);
        if (state_ != AsyncState::Running) {
            InvokeCallback();
        }
    }

    // Idempotent — false (no-op) if already in a terminal state.
    bool SetCompleted(T value) {
        if (state_ != AsyncState::Running) {
            return false;
        }
        value_.emplace(std::move(value));
        state_ = AsyncState::Completed;
        InvokeCallback();
        return true;
    }

    bool SetFailed(ScriptError error) {
        if (state_ != AsyncState::Running) {
            return false;
        }
        error_.emplace(std::move(error));
        state_ = AsyncState::Failed;
        InvokeCallback();
        return true;
    }

    bool Cancel() {
        if (state_ != AsyncState::Running) {
            return false;
        }
        state_ = AsyncState::Cancelled;
        InvokeCallback();
        return true;
    }

private:
    void InvokeCallback() {
        if (callback_) {
            callback_(*this);
        }
    }

    AsyncState state_ = AsyncState::Running;
    std::optional<T> value_;
    std::optional<ScriptError> error_;
    std::function<void(const AsyncResult&)> callback_;
};

} // namespace kb::library
