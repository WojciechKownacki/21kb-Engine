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

// LIB-097: Task/Coroutine model decision — this engine has THREE script
// backends (Native, Lua, Visual Graph), and a real "suspend an in-flight
// script call and resume it later" Coroutine needs backend-specific
// suspension machinery that does not exist anywhere yet on ANY of the three
// fronts (confirmed by research before implementing):
//   - Lua: every script call in this engine (ExecuteFunction,
//     PucLuaScriptRuntime.cpp) runs through lua_pcall, which runs a
//     function to completion and returns — real coroutine.yield support
//     that suspends a BEHAVIOUR'S OWN Tick call would require rebuilding
//     that call path on lua_newthread/lua_resume, a rewrite of the
//     engine's Lua calling convention itself, not an additive API.
//   - Visual Graph: VisualGraphRuntimeExecutor::ExecuteFunction always
//     walks the whole graph from entryNodeId to completion every
//     invocation — there is no persisted "which node was current" concept
//     anywhere, so a state-machine-graph Coroutine needs a new Wait node
//     kind AND a new per-instance persisted execution-position field,
//     neither of which exist.
//   - C++: this codebase has never used <coroutine>/std::coroutine_handle
//     anywhere (confirmed by a full-repo grep) — introducing real C++20
//     coroutines now would be a first-ever, unproven platform/toolchain
//     decision, not required to deliver a working Task primitive.
// Decision: SceneTasks/SceneTaskService below IS the C++ task adapter
// model, delivered now — a plain, hand-rolled poll object (no
// std::coroutine_handle, no suspension), mirroring SceneTimerService's
// exact per-frame-drive shape (LIB-095). The Lua generator model and the
// Visual Graph state-machine model are DELIBERATELY NOT implemented here —
// documented as the chosen future models, not fabricated — because a
// script cannot author the body of a Task/Coroutine without real
// suspension. Until then, only NATIVE C++ code can create a task (Start/
// StartFixedStep below have no script-facing equivalent); scripts can only
// observe/control an already-running task through Task.IsRunning/
// Task.Cancel and the TaskCompleted/TaskFailed events it dispatches — a
// real, non-fabricated capability (e.g. a native plugin starts a
// background operation and hands script the resulting handle to poll or
// cancel).
//
// LIB-098 ("yield na sekundy, fixed steps, event, asset load, scene load")
// scope decision, researched against this same model — of the five named
// "yield reasons," only TWO are genuinely distinct, buildable capabilities
// today:
//   - seconds: kb::library::MakeWaitSecondsTask (EngineLibraryTaskFactories.
//     hpp) — a Frame-domain poll closure, direct mirror of TimerRecord::
//     remainingSeconds.
//   - fixed steps: kb::library::MakeWaitFixedStepsTask, paired with the NEW
//     StartFixedStep/AdvanceFixedSteps plumbing below — this is the one
//     piece of real new engine work LIB-098 required (not just a factory
//     function), because nothing previously surfaced FixedTick's per-frame
//     step count to anything outside ScriptRuntimeSceneSystem::ExecuteFrame.
//   - event: DELIBERATELY NOT implemented — event delivery
//     (NativeScriptBackend::RegisterEvent/DispatchEvent) is push-only,
//     single-callback-per-(asset,eventName) with last-write-wins semantics;
//     nothing persists "did event X happen" for a poll to observe, and
//     building a proper multi-listener registry to support it is closer in
//     scope to Events.Subscribe (LIB-105) than to this task — documented as
//     a real, deferred gap, not silently faked with a hack that would break
//     under >1 concurrent listener.
//   - asset load / scene load: DELIBERATELY NOT given bespoke factory
//     helpers — both AssetManager::Load and Scene.Load already run fully
//     synchronously today (confirmed: SceneLoadedContentQueries::Progress
//     is a binary 1.0/0.0, never a real multi-frame ramp, per its own
//     LIB-071 doc comment), so "yielding" for either would degenerate to a
//     task that completes on its very first poll — indistinguishable from
//     any other trivial one-poll-and-done task and not worth a named
//     helper (a plain `[](float){ return TaskPollResult::Completed; }`
//     closure already covers it). Real multi-frame asset/scene-load
//     waiting requires an asynchronous AssetManager path that does not
//     exist anywhere in this engine — a much larger, separately-scoped
//     future change, not fabricated here.
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
    // number of FixedTick STEPS that occurred this frame (0, 1, or more —
    // never seconds). Needed because FixedTick's step count is computed
    // inside ScriptRuntimeSceneSystem::ExecuteFrame's own fixed-step loop
    // and was never previously surfaced anywhere Task could observe it —
    // a Frame-domain task driven by wall-clock delta cannot correctly
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
    // LIB-098: called once per frame by ScriptRuntimeSceneSystem, AFTER its
    // fixed-step loop, with the number of FixedTick steps that occurred
    // THIS frame (0 while paused — FixedTick's own accumulator already
    // freezes during scene pause, LIB-094, so stepCount is naturally 0
    // then; no separate pause check is needed). If stepCount is 0, no poll
    // is called at all this frame — the same "never call poll for a reason
    // that didn't happen" rule Advance follows above. Only polls
    // StartFixedStep tasks; Frame-domain tasks (Start) are untouched here.
    [[nodiscard]] std::vector<TaskCompletionRecord> AdvanceFixedSteps(std::size_t stepCount);

private:
    Scene& scene_;
};

} // namespace kb::scene
