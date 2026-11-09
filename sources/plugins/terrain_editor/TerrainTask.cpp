#include "TerrainTask.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace kb::terrain_editor {

struct TerrainBackgroundTask::State {
    kb::assets::TerrainAsset terrain;
    TerrainTaskControl control;
    std::mutex mutex;
    std::condition_variable finishedSignal;
    bool finished = false;
    bool succeeded = false;
    std::string error;
};

TerrainBackgroundTask::TerrainBackgroundTask(kb::assets::TerrainAsset input, Work work)
    : state_(std::make_shared<State>()) {
    state_->terrain = std::move(input);
    worker_ = std::thread([state = state_, work = std::move(work)] {
        std::string error;
        bool succeeded = false;
        try {
            succeeded = work(state->terrain, state->control, error);
        } catch (const std::exception& failure) {
            error = failure.what();
        }
        if (succeeded && state->control.Cancelled()) {
            succeeded = false;
            error = "cancelled";
        }
        {
            const std::scoped_lock lock{ state->mutex };
            state->succeeded = succeeded;
            state->error = std::move(error);
            state->finished = true;
        }
        state->finishedSignal.notify_all();
    });
}

TerrainBackgroundTask::~TerrainBackgroundTask() {
    Cancel();
    if (worker_.joinable()) worker_.join();
}

void TerrainBackgroundTask::Cancel() noexcept {
    state_->control.cancel.store(true, std::memory_order_relaxed);
}

bool TerrainBackgroundTask::Finished() const noexcept {
    const std::scoped_lock lock{ state_->mutex };
    return state_->finished;
}

float TerrainBackgroundTask::Progress() const noexcept {
    return state_->control.Progress();
}

std::optional<kb::assets::TerrainAsset> TerrainBackgroundTask::Wait(double seconds, std::string* error) {
    std::unique_lock lock{ state_->mutex };
    bool timedOut = false;
    if (seconds > 0.0 && !state_->finishedSignal.wait_for(lock, std::chrono::duration<double>(seconds), [this] { return state_->finished; })) {
        timedOut = true;
        Cancel();
    }
    state_->finishedSignal.wait(lock, [this] { return state_->finished; });
    if (worker_.joinable()) {
        lock.unlock();
        worker_.join();
        lock.lock();
    }
    if (!state_->succeeded) {
        if (error != nullptr) *error = timedOut ? "stopped after the time limit" : (state_->error.empty() ? "the job failed" : state_->error);
        return std::nullopt;
    }
    return std::move(state_->terrain);
}

} // namespace kb::terrain_editor
