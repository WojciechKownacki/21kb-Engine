#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class Scene;

// LIB-095: one timer that fired during a single SceneTimers::Advance call —
// the raw payload kb::script::ScriptRuntimeSceneSystem turns into a real
// "TimerFired" ScriptEvent once per frame (mirrors SceneLifecycleEventRecord/
// DrainPendingLifecycleEvents' own LIB-073 shape).
struct TimerFiredRecord {
    std::uint64_t id = 0U;
    SceneEntity owner{};
};

// LIB-095: Timer.Once/Repeat/Cancel/Pause/Resume's engine-side facade.
// TimerHandle = a monotonically increasing per-scene std::uint64_t id
// (SceneState::nextTimerId, never reused within a scene's lifetime — same
// convention as SceneState::nextLoadedSceneId, LIB-071) threaded across the
// script boundary as ScriptValueType::Hash, deliberately NOT a
// generation-checked cross-script-boundary handle registry (that shape was
// explicitly deferred by LIB-058's own collection-handle decision; a timer
// id does not need it, since ids are never reused a stale id can never
// collide with a live one).
class SceneTimers {
public:
    explicit SceneTimers(Scene& scene) noexcept;

    // Returns 0 (never a valid id) if delaySeconds/intervalSeconds <= 0, or
    // if the scene already holds kDefaultLibraryInputLimits.maxCollectionSize
    // live timers.
    [[nodiscard]] std::uint64_t Once(float delaySeconds, SceneEntity owner) noexcept;
    [[nodiscard]] std::uint64_t Repeat(float intervalSeconds, SceneEntity owner) noexcept;
    // Idempotent — false if `id` names no currently live timer (already
    // fired-and-removed one-shot, already cancelled, or never existed).
    [[nodiscard]] bool Cancel(std::uint64_t id) noexcept;
    // Pause/Resume are "set" operations, not "changed" operations (mirrors
    // World.SetActive, LIB-068) — they return true whenever `id` names a
    // live timer, even if it was already in the requested pause state.
    [[nodiscard]] bool Pause(std::uint64_t id) noexcept;
    [[nodiscard]] bool Resume(std::uint64_t id) noexcept;
    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;

    // LIB-095: called once per frame by kb::script::ScriptRuntimeSceneSystem
    // with the same raw (already non-negative-clamped) deltaSeconds it uses
    // for Tick — Advance itself applies the exact Time.Delta scale/pause
    // rule (scale = IsPlaying() ? TimeScale() : 0) before decrementing any
    // timer, so a timer's notion of elapsed time is always identical to
    // what Time.Delta reports for the same frame. A timer whose owner is no
    // longer alive is silently auto-cancelled (removed, no fire) the moment
    // that's detected, regardless of remaining time or pause state — no
    // dangling callback for a dead entity. A one-shot timer is removed from
    // storage after firing.
    // LIB-096: same-time ordering — when multiple timers become due within
    // one Advance() call, `fired` lists them in CREATION order (the order
    // Timer.Once/Timer.Repeat were called), never by remaining-time
    // magnitude. Long-frame catch-up — a repeating timer that fell behind by
    // more than one whole interval fires once per missed interval
    // (consecutive entries in `fired`, still within that timer's own slot in
    // creation order) up to a bounded cap (SceneTimerService's internal
    // kMaxCatchUpFiresPerAdvance); once that cap is hit, the remaining
    // backlog is honestly dropped (reset to exactly one fresh interval)
    // rather than replayed on a later frame or left to grow unboundedly —
    // the same spiral-of-death guard ScriptRuntimeFrameSettings::
    // maxFixedStepsPerFrame already applies to FixedTick.
    [[nodiscard]] std::vector<TimerFiredRecord> Advance(float deltaSeconds);

private:
    Scene& scene_;
};

} // namespace kb::scene
