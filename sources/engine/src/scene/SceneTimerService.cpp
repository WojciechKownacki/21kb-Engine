#include "scene/SceneTimerService.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneRuntimeService.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>

namespace kb::scene {
namespace {

// LIB-095: mirrors LIB-058's kDefaultLibraryInputLimits.maxCollectionSize
// (kb::library::EngineLibraryInputLimits.hpp) — the SAME numeric policy
// value, duplicated as a plain constant rather than an #include, because
// kb::scene must never depend on kb::library (kb::library wraps kb::scene,
// never the other way around).
constexpr std::size_t kMaxLiveTimers = 4096U;

// LIB-096: bounds how many consecutive intervals a single Timer.Repeat can
// "catch up" on within ONE Advance() call — mirrors the exact same
// spiral-of-death guard SceneRuntimeFixedStepSettings::maxFixedStepsPerFrame
// applies to the authoritative FixedTick/physics accumulator. Without a
// bound, a tiny intervalSeconds under one huge deltaSeconds
// (a debugger breakpoint, an asset-load stall) would fire thousands of
// TimerFired events in a single frame; with no bound at all in the OTHER
// direction (LIB-095's original flat reset), an overdue repeating timer
// silently lost all of that backlog instead. This constant is the explicit
// middle ground: fire up to this many times to catch up, then honestly
// drop the remaining backlog (documented below, not silent).
constexpr std::size_t kMaxCatchUpFiresPerAdvance = 8U;

// LIB-099: cancellation propagation — a timer's owner being destroyed
// already auto-cancelled it (LIB-095); this widens the SAME check to also
// cover the owner being DEACTIVATED (kb::scene::SceneEntityService::
// IsActive, i.e. World.SetActive(owner, false), LIB-068) — an owner that
// still exists but is no longer active is, from a gameplay-visible
// standpoint, exactly as "gone" as a destroyed one, so a timer scheduled
// on its behalf should stop the same way. Scene.Unload needed NO separate
// handling: it destroys its root entity's whole hierarchy via the same
// SceneEntityDestructionService cascade LIB-070 already established, so
// any timer owned by an entity inside an unloaded scene is caught here via
// the IsAlive half of this same check on the very next Advance() call —
// confirmed by RunSceneTimerCancellationPropagationTest, not assumed.
[[nodiscard]] bool OwnerGone(const Scene& scene, SceneEntity owner) noexcept {
    return owner.IsValid() && (!SceneEntityService::IsAlive(scene, owner) || !SceneEntityService::IsActive(scene, owner));
}

} // namespace

std::uint64_t SceneTimerService::Once(Scene& scene, float delaySeconds, SceneEntity owner, SceneEntity creator) noexcept {
    if (delaySeconds <= 0.0F) {
        return 0U;
    }
    SceneState& state = SceneAccess::State(scene);
    if (state.timers.size() >= kMaxLiveTimers) {
        return 0U;
    }
    const std::uint64_t id = state.nextTimerId++;
    state.timers.push_back(SceneState::TimerRecord{
        .id = id,
        .owner = owner,
        .remainingSeconds = delaySeconds,
        .intervalSeconds = 0.0F,
        .repeating = false,
        .paused = false,
        .creator = creator,
    });
    return id;
}

std::uint64_t SceneTimerService::Repeat(Scene& scene, float intervalSeconds, SceneEntity owner, SceneEntity creator) noexcept {
    if (intervalSeconds <= 0.0F) {
        return 0U;
    }
    SceneState& state = SceneAccess::State(scene);
    if (state.timers.size() >= kMaxLiveTimers) {
        return 0U;
    }
    const std::uint64_t id = state.nextTimerId++;
    state.timers.push_back(SceneState::TimerRecord{
        .id = id,
        .owner = owner,
        .remainingSeconds = intervalSeconds,
        .intervalSeconds = intervalSeconds,
        .repeating = true,
        .paused = false,
        .creator = creator,
    });
    return id;
}

bool SceneTimerService::Cancel(Scene& scene, std::uint64_t id) noexcept {
    SceneState& state = SceneAccess::State(scene);
    const auto iterator = std::find_if(state.timers.begin(), state.timers.end(), [id](const SceneState::TimerRecord& timer) {
        return timer.id == id;
    });
    if (iterator == state.timers.end()) {
        return false;
    }
    state.timers.erase(iterator);
    return true;
}

bool SceneTimerService::Pause(Scene& scene, std::uint64_t id) noexcept {
    SceneState& state = SceneAccess::State(scene);
    for (SceneState::TimerRecord& timer : state.timers) {
        if (timer.id == id) {
            timer.paused = true;
            return true;
        }
    }
    return false;
}

bool SceneTimerService::Resume(Scene& scene, std::uint64_t id) noexcept {
    SceneState& state = SceneAccess::State(scene);
    for (SceneState::TimerRecord& timer : state.timers) {
        if (timer.id == id) {
            timer.paused = false;
            return true;
        }
    }
    return false;
}

bool SceneTimerService::Exists(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return std::any_of(state.timers.begin(), state.timers.end(), [id](const SceneState::TimerRecord& timer) {
        return timer.id == id;
    });
}

SceneEntity SceneTimerService::Creator(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    const auto iterator = std::find_if(state.timers.begin(), state.timers.end(), [id](const SceneState::TimerRecord& timer) {
        return timer.id == id;
    });
    return iterator == state.timers.end() ? SceneEntity{} : iterator->creator;
}

std::vector<TimerFiredRecord> SceneTimerService::Advance(Scene& scene, float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene);
    // LIB-095: identical scale/pause rule to Time.Delta (ScriptTimeApi.cpp)
    // — a timer's notion of elapsed time is always exactly what script code
    // observes through Time.Delta for the same frame.
    const float scale = SceneRuntimeService::IsPlaying(scene) ? SceneRuntimeService::TimeScale(scene) : 0.0F;
    const float effectiveDelta = deltaSeconds * scale;

    // LIB-096: same-time ordering — `state.timers` is walked in ITS OWN
    // storage order, which is creation order among still-live timers
    // (Once/Repeat push_back; erase below only ever removes an earlier
    // element and never reorders survivors) — so when several timers
    // become due within the same Advance() call, `fired` lists them in the
    // order they were CREATED (Timer.Once/Timer.Repeat call order), never
    // by remaining-time magnitude or interval size. This mirrors LIB-060's
    // own precedent of turning an already-true, previously-undocumented
    // storage-order behavior into an explicit, tested contract rather than
    // inventing a new ordering scheme.
    std::vector<TimerFiredRecord> fired;
    std::size_t index = 0U;
    while (index < state.timers.size()) {
        SceneState::TimerRecord& timer = state.timers[index];
        if (OwnerGone(scene, timer.owner)) {
            state.timers.erase(state.timers.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        if (!timer.paused) {
            timer.remainingSeconds -= effectiveDelta;
        }
        if (timer.remainingSeconds > 0.0F) {
            ++index;
            continue;
        }
        fired.push_back(TimerFiredRecord{ .id = timer.id, .owner = timer.owner });
        if (!timer.repeating) {
            state.timers.erase(state.timers.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        // LIB-096: catch-up policy for a repeating timer that fell behind
        // by more than one whole interval (a long/stalled frame) — keep
        // firing (each one a separate TimerFired, consecutive within this
        // timer's own slot in `fired`) until it's caught up OR the
        // kMaxCatchUpFiresPerAdvance bound is hit, whichever comes first.
        // Hitting the bound HONESTLY DROPS the remaining backlog (resets to
        // one fresh interval) rather than either replaying it later
        // (which would just move the spiral to a future frame) or firing
        // it all at once with no bound (the actual spiral-of-death risk).
        std::size_t catchUpFires = 1U;
        while (timer.remainingSeconds <= 0.0F && catchUpFires < kMaxCatchUpFiresPerAdvance) {
            timer.remainingSeconds += timer.intervalSeconds;
            if (timer.remainingSeconds <= 0.0F) {
                fired.push_back(TimerFiredRecord{ .id = timer.id, .owner = timer.owner });
            }
            ++catchUpFires;
        }
        timer.remainingSeconds = timer.remainingSeconds <= 0.0F ? timer.intervalSeconds : timer.remainingSeconds;
        ++index;
    }
    return fired;
}

} // namespace kb::scene
