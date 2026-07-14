#pragma once

#include "engine/scene/SceneEntity.hpp"

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
// script cannot author the body of a Task/Coroutine until LIB-098 delivers
// a real yield mechanism (seconds/fixed-steps/event/asset-load/scene-load).
// Until then, only NATIVE C++ code can create a task (Start below has no
// script-facing equivalent); scripts can only observe/control an
// already-running task through Task.IsRunning/Task.Cancel and the
// TaskCompleted/TaskFailed events it dispatches — a real, non-fabricated
// capability (e.g. a native plugin starts a background operation and hands
// script the resulting handle to poll or cancel).
class SceneTasks {
public:
    explicit SceneTasks(Scene& scene) noexcept;

    // Returns 0 (never a valid id) if `poll` is empty, or if the scene
    // already holds its maximum number of live tasks. NATIVE-ONLY — see
    // the class doc comment above for why no script-facing Task.Start
    // exists yet.
    [[nodiscard]] std::uint64_t Start(std::function<TaskPollResult(float)> poll, SceneEntity owner);
    // Idempotent — false if `id` names no currently live task (already
    // completed/failed/cancelled, or never existed).
    [[nodiscard]] bool Cancel(std::uint64_t id) noexcept;
    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;

    // LIB-097: called once per frame by kb::script::ScriptRuntimeSceneSystem
    // with the same raw deltaSeconds it uses for Tick — Advance applies the
    // exact Time.Delta scale/pause rule (scale = IsPlaying() ? TimeScale()
    // : 0) and, while the scene is paused, does not call ANY task's poll
    // callback at all that frame (mirrors FixedTick freezing during pause,
    // LIB-094 — a paused game must not let a task's poll observe or react
    // to scene state that shouldn't be changing). A task whose owner is no
    // longer alive is silently auto-cancelled (removed, no completion
    // record) the moment that's detected, regardless of pause state — the
    // identical pattern Timer's owner entity uses (LIB-095).
    [[nodiscard]] std::vector<TaskCompletionRecord> Advance(float deltaSeconds);

private:
    Scene& scene_;
};

} // namespace kb::scene
