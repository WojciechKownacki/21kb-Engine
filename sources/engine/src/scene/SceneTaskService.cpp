#include "scene/SceneTaskService.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneRuntimeService.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <utility>

namespace kb::scene {
namespace {

// LIB-097: mirrors LIB-095's kMaxLiveTimers — same numeric policy value,
// same reasoning (kb::scene must never depend on kb::library).
constexpr std::size_t kMaxLiveTasks = 4096U;

} // namespace

std::uint64_t SceneTaskService::Start(Scene& scene, std::function<TaskPollResult(float)> poll, SceneEntity owner) {
    if (!poll) {
        return 0U;
    }
    SceneState& state = SceneAccess::State(scene);
    if (state.tasks.size() >= kMaxLiveTasks) {
        return 0U;
    }
    const std::uint64_t id = state.nextTaskId++;
    state.tasks.push_back(SceneState::TaskRecord{
        .id = id,
        .owner = owner,
        .poll = std::move(poll),
    });
    return id;
}

bool SceneTaskService::Cancel(Scene& scene, std::uint64_t id) noexcept {
    SceneState& state = SceneAccess::State(scene);
    const auto iterator = std::find_if(state.tasks.begin(), state.tasks.end(), [id](const SceneState::TaskRecord& task) {
        return task.id == id;
    });
    if (iterator == state.tasks.end()) {
        return false;
    }
    state.tasks.erase(iterator);
    return true;
}

bool SceneTaskService::Exists(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return std::any_of(state.tasks.begin(), state.tasks.end(), [id](const SceneState::TaskRecord& task) {
        return task.id == id;
    });
}

std::vector<TaskCompletionRecord> SceneTaskService::Advance(Scene& scene, float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene);
    const bool isPlaying = SceneRuntimeService::IsPlaying(scene);
    // LIB-097: while paused, no task's poll is called at all this frame
    // (not even with a zeroed delta) — mirrors FixedTick freezing entirely
    // during scene pause (LIB-094), so a paused game never lets a task's
    // poll observe or react to state that shouldn't be changing.
    if (!isPlaying) {
        // Owner-death auto-cancel still applies even while paused (an
        // entity can be destroyed by native code regardless of play state).
        std::vector<TaskCompletionRecord> none;
        std::size_t index = 0U;
        while (index < state.tasks.size()) {
            SceneState::TaskRecord& task = state.tasks[index];
            if (task.owner.IsValid() && !SceneEntityService::IsAlive(scene, task.owner)) {
                state.tasks.erase(state.tasks.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            ++index;
        }
        return none;
    }

    const float effectiveDelta = deltaSeconds * SceneRuntimeService::TimeScale(scene);

    std::vector<TaskCompletionRecord> completed;
    std::size_t index = 0U;
    while (index < state.tasks.size()) {
        SceneState::TaskRecord& task = state.tasks[index];
        if (task.owner.IsValid() && !SceneEntityService::IsAlive(scene, task.owner)) {
            state.tasks.erase(state.tasks.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        const TaskPollResult result = task.poll(effectiveDelta);
        if (result == TaskPollResult::Running) {
            ++index;
            continue;
        }
        completed.push_back(TaskCompletionRecord{
            .id = task.id,
            .owner = task.owner,
            .succeeded = result == TaskPollResult::Completed,
        });
        state.tasks.erase(state.tasks.begin() + static_cast<std::ptrdiff_t>(index));
    }
    return completed;
}

} // namespace kb::scene
