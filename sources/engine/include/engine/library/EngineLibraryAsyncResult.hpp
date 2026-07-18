#pragma once

#include "engine/library/EngineLibraryError.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
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
// "Callback on the correct thread" (this task's own wording) — an ENFORCED
// guarantee, not merely an accident of the engine having no threading today:
// each AsyncResult captures its OWNER thread (the thread that constructs it,
// which owns the Scene) and, if SetCompleted/SetFailed/Cancel is ever called
// from a DIFFERENT thread (e.g. a future background asset/scene loader —
// LIB-098's deferred infra), does NOT invoke the callback on that foreign
// thread. It instead defers the callback until the owner drains it via
// Poll() on its own thread. When the terminal transition happens ON the
// owner thread (the only case that occurs in the engine today, since nothing
// completes an AsyncResult off-thread yet) the callback still fires
// immediately and synchronously, exactly as before — so this costs the
// common path nothing while making the cross-thread contract real rather
// than assumed. A std::mutex guards the transition/callback state and the
// state is atomic for lock-free State()/IsRunning()/Succeeded() reads, so a
// poll from the owner thread (e.g. MakeTaskPollFromAsyncResult) races safely
// against an off-thread SetCompleted. This makes AsyncResult non-copyable and
// non-movable (it owns a mutex) — every use in the engine holds it by
// reference or as a stable member, never copies it.
template <typename T>
class AsyncResult final {
public:
    AsyncResult() noexcept
        : ownerThread_(std::this_thread::get_id()) {}

    AsyncResult(const AsyncResult&) = delete;
    AsyncResult& operator=(const AsyncResult&) = delete;
    AsyncResult(AsyncResult&&) = delete;
    AsyncResult& operator=(AsyncResult&&) = delete;

    [[nodiscard]] AsyncState State() const noexcept { return state_.load(std::memory_order_acquire); }
    [[nodiscard]] bool IsRunning() const noexcept { return State() == AsyncState::Running; }
    [[nodiscard]] bool Succeeded() const noexcept { return State() == AsyncState::Completed; }

    // Precondition: Succeeded(). Throws std::bad_optional_access otherwise
    // (via std::optional::value()), same throw-on-misuse contract as
    // kb::library::Result<T>::Value() (LIB-061). value_ is written before the
    // release-store of state_, so a caller that has observed Succeeded()
    // (acquire) sees the value.
    [[nodiscard]] const T& Value() const& { return value_.value(); }

    // Precondition: State() == AsyncState::Failed. Same throw-on-misuse
    // contract as Value() above.
    [[nodiscard]] const ScriptError& Error() const& { return error_.value(); }

    // Registers the completion callback — a SINGLE listener (registering
    // again replaces the previous one, the same single-callback convention
    // NativeScriptBackend::RegisterEvent already uses), called EXACTLY once,
    // ALWAYS on the owner thread. If already terminal when registered: fires
    // immediately (synchronously) when this OnComplete call is itself on the
    // owner thread, otherwise is deferred to the next owner-thread Poll() —
    // a late listener never misses the result, and never runs off-thread.
    void OnComplete(std::function<void(const AsyncResult&)> callback) {
        std::unique_lock<std::mutex> lock(mutex_);
        callback_ = std::move(callback);
        if (state_.load(std::memory_order_relaxed) != AsyncState::Running) {
            FireOrDefer(lock);
        }
    }

    // Idempotent — false (no-op) if already in a terminal state. The callback
    // fires here only when called on the owner thread; an off-thread call
    // defers it to Poll() (see the class doc comment).
    bool SetCompleted(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (state_.load(std::memory_order_relaxed) != AsyncState::Running) {
            return false;
        }
        value_.emplace(std::move(value));
        state_.store(AsyncState::Completed, std::memory_order_release);
        FireOrDefer(lock);
        return true;
    }

    bool SetFailed(ScriptError error) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (state_.load(std::memory_order_relaxed) != AsyncState::Running) {
            return false;
        }
        error_.emplace(std::move(error));
        state_.store(AsyncState::Failed, std::memory_order_release);
        FireOrDefer(lock);
        return true;
    }

    bool Cancel() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (state_.load(std::memory_order_relaxed) != AsyncState::Running) {
            return false;
        }
        state_.store(AsyncState::Cancelled, std::memory_order_release);
        FireOrDefer(lock);
        return true;
    }

    // Drains a callback deferred by an off-thread terminal transition, on the
    // caller's (owner's) thread. A no-op when nothing is pending — safe to
    // call every frame. The common single-threaded path never needs this (the
    // callback already fired synchronously inside SetCompleted/etc.).
    void Poll() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!pendingCallback_) {
            return;
        }
        pendingCallback_ = false;
        const std::function<void(const AsyncResult&)> callback = callback_;
        lock.unlock();
        if (callback) {
            callback(*this);
        }
    }

private:
    // Called holding `lock` after a terminal transition (or from OnComplete
    // when already terminal). Fires the callback NOW iff on the owner thread,
    // releasing the lock first so the callback may re-enter safely; otherwise
    // marks it pending for the next owner-thread Poll().
    void FireOrDefer(std::unique_lock<std::mutex>& lock) {
        if (std::this_thread::get_id() == ownerThread_) {
            pendingCallback_ = false;
            const std::function<void(const AsyncResult&)> callback = callback_;
            lock.unlock();
            if (callback) {
                callback(*this);
            }
        } else {
            pendingCallback_ = true;
        }
    }

    std::atomic<AsyncState> state_{ AsyncState::Running };
    std::optional<T> value_;
    std::optional<ScriptError> error_;
    std::function<void(const AsyncResult&)> callback_;
    std::thread::id ownerThread_;
    bool pendingCallback_ = false;
    mutable std::mutex mutex_;
};

} // namespace kb::library
