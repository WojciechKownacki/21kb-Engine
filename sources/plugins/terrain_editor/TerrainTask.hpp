#pragma once

#include "engine/assets/TerrainAsset.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace kb::terrain_editor {

// What a long terrain job (a generator, an erosion) shares with whoever watches it: the request to
// stop, how far it has come, and how many threads it may use.
struct TerrainTaskControl {
    std::atomic<bool> cancel{ false };
    // 0 means every hardware thread.
    std::uint32_t maxWorkers = 0U;

    [[nodiscard]] bool Cancelled() const noexcept { return cancel.load(std::memory_order_relaxed); }
    void Begin(std::uint64_t totalUnits) noexcept {
        total_.store(std::max<std::uint64_t>(totalUnits, 1U), std::memory_order_relaxed);
        done_.store(0U, std::memory_order_relaxed);
    }
    void Advance(std::uint64_t units) noexcept { done_.fetch_add(units, std::memory_order_relaxed); }
    // 0..1.
    [[nodiscard]] float Progress() const noexcept {
        const std::uint64_t total = total_.load(std::memory_order_relaxed);
        return std::min(1.0F, static_cast<float>(done_.load(std::memory_order_relaxed)) / static_cast<float>(total));
    }

private:
    std::atomic<std::uint64_t> total_{ 1U };
    std::atomic<std::uint64_t> done_{ 0U };
};

// Runs a job on a worker thread over its own copy of a terrain, so the editor keeps running and
// the terrain on screen is untouched until the job has succeeded. Cancelling (or destroying the
// task) stops the job; a cancelled or failed job delivers no terrain.
class TerrainBackgroundTask final {
public:
    using Work = std::function<bool(kb::assets::TerrainAsset&, TerrainTaskControl&, std::string&)>;

    TerrainBackgroundTask(kb::assets::TerrainAsset input, Work work);
    ~TerrainBackgroundTask();
    TerrainBackgroundTask(const TerrainBackgroundTask&) = delete;
    TerrainBackgroundTask& operator=(const TerrainBackgroundTask&) = delete;

    void Cancel() noexcept;
    [[nodiscard]] bool Finished() const noexcept;
    [[nodiscard]] float Progress() const noexcept;
    // Waits for the job, at most `seconds` when that is positive (then cancels it and still
    // waits for it to stop). The terrain when the job succeeded; otherwise nothing, with the reason.
    [[nodiscard]] std::optional<kb::assets::TerrainAsset> Wait(double seconds, std::string* error);

private:
    struct State;
    std::shared_ptr<State> state_;
    std::thread worker_;
};

} // namespace kb::terrain_editor
