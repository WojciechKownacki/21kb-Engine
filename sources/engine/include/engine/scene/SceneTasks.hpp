#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace kb::scene {

class Scene;

// LIB-097: the result a Task's poll callback reports each frame it's
// called — Running means "call me again next frame," Completed/Failed are
// terminal (the task is removed from storage the moment either is seen,
// mirroring Timer's own "one-shot fires once, then gone" convention).
enum class TaskPollResult {
    Running,
    Completed,
    Failed,
};

// LIB-097: one task that reached a terminal state during a single
// SceneTasks::Advance call — the raw payload
// kb::script::ScriptRuntimeSceneSystem turns into a real "TaskCompleted"/
// "TaskFailed" ScriptEvent once per frame (mirrors TimerFiredRecord's own
// LIB-095 shape).
struct TaskCompletionRecord {
    std::uint64_t id = 0U;
    SceneEntity owner{};
    bool succeeded = false;
};

// LIB-097: Task/Coroutine model decision. The three supported execution
// models deliberately match each backend instead of imposing one unsafe
// abstraction on all of them:
//   - Lua lifecycle and event entries run as Lua generator threads. A
//     coroutine.yield() suspends the entry and the next call of that same
//     entry resumes it (PucLuaScriptRuntime::ExecuteFunction).
//   - Visual Graph has the Wait node. It persists the next node per event
//     in VisualGraphRuntimeExecutionContext and resumes that node on the
//     next invocation of the same lifecycle/custom-event function.
//   - Native C++ uses SceneTasks/SceneTaskService: a hand-rolled poll
//     adapter, not std::coroutine_handle, with the same per-frame drive
//     shape as SceneTimerService (LIB-095).
// A SceneTasks body is intentionally native-only: Start/StartFixedStep have
// no script-facing equivalent. Scripts can observe or cancel a native task
// through Task.IsRunning/Task.Cancel and receive TaskCompleted/TaskFailed.
//
// LIB-098 supplies five non-blocking reason adapters in
// EngineLibraryTaskFactories.hpp: seconds, real FixedTick step count, an
// observed ScriptEventBus event, an AssetManager cache transition, and a
// loaded-scene record appearing. Event observations are shared sequence
// objects rather than retained callbacks, so cancellation and host teardown
// are lifetime-safe. Asset/scene waits deliberately only OBSERVE state:
// they never start synchronous Load/LoadOpaque work from a task poll and
// therefore never hide blocking I/O inside ScriptRuntimeSceneSystem's
// per-frame Advance. Loading remains the responsibility of the owning
// subsystem (LIB-155 covers a genuinely asynchronous asset-load initiator).
class SceneTasks {
public:
    explicit SceneTasks(Scene& scene) noexcept;

    // Returns 0 (never a valid id) if `poll` is empty, or if the scene
    // already holds its maximum number of live tasks. NATIVE-ONLY — see
    // the class doc comment above for why no script-facing Task.Start
    // exists yet. Frame-domain: poll's float argument is scaled/pause-aware
    // elapsed SECONDS (see Advance below). `creator` (LIB-101) is an
    // optional creation-site diagnostic the native caller may supply — see
    // TaskRecord::creator's own doc comment (SceneState.hpp) for the full
    // reasoning.
    [[nodiscard]] std::uint64_t Start(std::function<TaskPollResult(float)> poll, SceneEntity owner, SceneEntity creator = {});
    // LIB-098: identical to Start above, EXCEPT this task is driven by
    // AdvanceFixedSteps instead of Advance — poll's float argument is the
    // number of FixedTick STEPS being advanced (never seconds). The installed
    // ScriptRuntimeSceneSystem drives it from the authoritative scene fixed
    // callback; a Frame-domain task driven by wall-clock delta cannot correctly
    // count "N fixed steps," since fixed-step count and elapsed seconds
    // are decoupled (0 to maxFixedStepsPerFrame steps can occur for the
    // same deltaSeconds, depending on accumulated backlog).
    [[nodiscard]] std::uint64_t StartFixedStep(std::function<TaskPollResult(float)> poll, SceneEntity owner, SceneEntity creator = {});
    // Idempotent — false if `id` names no currently live task (already
    // completed/failed/cancelled, or never existed). Works for tasks from
    // either Start or StartFixedStep.
    [[nodiscard]] bool Cancel(std::uint64_t id) noexcept;
    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    // LIB-101: returns an invalid SceneEntity if `id` names no currently
    // live task, or if it was created without a creator.
    [[nodiscard]] SceneEntity Creator(std::uint64_t id) const noexcept;

    // LIB-097: called once per frame by kb::script::ScriptRuntimeSceneSystem
    // with the same raw deltaSeconds it uses for Tick — Advance applies the
    // exact Time.Delta scale/pause rule (scale = IsPlaying() ? TimeScale()
    // : 0) and, while the scene is paused, does not call ANY task's poll
    // callback at all that frame (mirrors FixedTick freezing during pause,
    // LIB-094 — a paused game must not let a task's poll observe or react
    // to scene state that shouldn't be changing). A task whose owner is no
    // longer alive OR no longer active (LIB-099: World.SetActive(owner,
    // false)/LIB-068) is silently auto-cancelled (removed, no completion
    // record) the moment that's detected, regardless of pause state — the
    // identical pattern Timer's owner entity uses (LIB-095/099). Scene.
    // Unload needs no separate handling — see SceneTimers::Advance's own
    // doc comment for why. Only polls Frame-domain tasks (Start);
    // StartFixedStep tasks are untouched here.
    [[nodiscard]] std::vector<TaskCompletionRecord> Advance(float deltaSeconds);
    // LIB-098: called by ScriptRuntimeSceneSystem after authoritative
    // FixedTick steps (or by direct ExecuteFrame's compatible loop). If
    // stepCount is 0, no poll
    // is called at all this frame — the same "never call poll for a reason
    // that didn't happen" rule Advance follows above. Only polls
    // StartFixedStep tasks; Frame-domain tasks (Start) are untouched here.
    [[nodiscard]] std::vector<TaskCompletionRecord> AdvanceFixedSteps(std::size_t stepCount);

private:
    Scene& scene_;
};

} // namespace kb::scene
