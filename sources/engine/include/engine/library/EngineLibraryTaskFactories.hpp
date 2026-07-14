#pragma once

#include "engine/library/EngineLibraryAsyncResult.hpp"
#include "engine/scene/SceneTasks.hpp"

#include <cstddef>
#include <functional>
#include <utility>

namespace kb::library {

// LIB-098: ready-made kb::scene::SceneTasks poll closures for the two
// "yield reasons" that are genuinely distinct, buildable capabilities
// today — see kb::scene::SceneTasks' own class doc comment for the full
// scope decision covering all five originally-named reasons (seconds/
// fixed-steps/event/asset-load/scene-load) and why event/asset-load/
// scene-load are NOT given factories here.

// Pass the result to SceneTasks::Start(...). Completes once `seconds` of
// Frame-domain (scaled/pause-aware) time has elapsed — a non-positive
// `seconds` completes on the very first poll, mirroring how a zero-length
// wait should behave (immediately satisfied, not an error — this is a
// native C++ convenience closure, not a validated script-facing boundary
// like Timer.Once's own delay>0 rejection).
[[nodiscard]] inline std::function<kb::scene::TaskPollResult(float)> MakeWaitSecondsTask(float seconds) {
    return [remaining = seconds](float deltaSeconds) mutable -> kb::scene::TaskPollResult {
        remaining -= deltaSeconds;
        return remaining <= 0.0F ? kb::scene::TaskPollResult::Completed : kb::scene::TaskPollResult::Running;
    };
}

// Pass the result to SceneTasks::StartFixedStep(...) — NOT Start (this
// closure interprets its float argument as a FixedTick step COUNT, not
// seconds; calling it from the wrong Advance path silently produces wrong
// results, so double-check the pairing at the call site). Completes once
// `steps` FixedTick steps have elapsed; a `steps` of 0 completes on the
// first non-zero-stepCount poll (there is no synchronous "already done"
// path — a task never resolves before its first real Advance call, by
// design, mirroring Timer).
[[nodiscard]] inline std::function<kb::scene::TaskPollResult(float)> MakeWaitFixedStepsTask(std::size_t steps) {
    return [remaining = steps](float stepsElapsed) mutable -> kb::scene::TaskPollResult {
        const std::size_t elapsed = stepsElapsed > 0.0F ? static_cast<std::size_t>(stepsElapsed) : 0U;
        remaining = elapsed >= remaining ? 0U : remaining - elapsed;
        return remaining == 0U ? kb::scene::TaskPollResult::Completed : kb::scene::TaskPollResult::Running;
    };
}

// LIB-100: bridges an AsyncResult<T> to SceneTasks' poll-based Task
// model — pass the returned closure to SceneTasks::Start(...); it reports
// Running/Completed while `result` is Running/Completed, and Failed for
// BOTH AsyncState::Failed and AsyncState::Cancelled (TaskPollResult itself
// has no distinct Cancelled state — a caller that needs to tell the two
// apart should inspect `result.State()` directly after the task's
// TaskFailed event fires, rather than through the task's own result).
// LIFETIME: `result` is captured BY REFERENCE — it must outlive every
// Advance() call that polls the returned closure (typically achieved by
// the caller owning the AsyncResult<T> as a long-lived member, not a local
// that goes out of scope before the task finishes).
template <typename T>
[[nodiscard]] std::function<kb::scene::TaskPollResult(float)> MakeTaskPollFromAsyncResult(const AsyncResult<T>& result) {
    return [&result](float) -> kb::scene::TaskPollResult {
        switch (result.State()) {
            case AsyncState::Running:
                return kb::scene::TaskPollResult::Running;
            case AsyncState::Completed:
                return kb::scene::TaskPollResult::Completed;
            case AsyncState::Failed:
            case AsyncState::Cancelled:
            default:
                return kb::scene::TaskPollResult::Failed;
        }
    };
}

} // namespace kb::library
