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

} // namespace

std::uint64_t SceneTimerService::Once(Scene& scene, float delaySeconds, SceneEntity owner) noexcept {
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
    });
    return id;
}

std::uint64_t SceneTimerService::Repeat(Scene& scene, float intervalSeconds, SceneEntity owner) noexcept {
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

std::vector<TimerFiredRecord> SceneTimerService::Advance(Scene& scene, float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene);
    // LIB-095: identical scale/pause rule to Time.Delta (ScriptTimeApi.cpp)
    // — a timer's notion of elapsed time is always exactly what script code
    // observes through Time.Delta for the same frame.
    const float scale = SceneRuntimeService::IsPlaying(scene) ? SceneRuntimeService::TimeScale(scene) : 0.0F;
    const float effectiveDelta = deltaSeconds * scale;

    std::vector<TimerFiredRecord> fired;
    std::size_t index = 0U;
    while (index < state.timers.size()) {
        SceneState::TimerRecord& timer = state.timers[index];
        if (timer.owner.IsValid() && !SceneEntityService::IsAlive(scene, timer.owner)) {
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
        if (timer.repeating) {
            timer.remainingSeconds = timer.intervalSeconds;
            ++index;
        } else {
            state.timers.erase(state.timers.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
    return fired;
}

} // namespace kb::scene
