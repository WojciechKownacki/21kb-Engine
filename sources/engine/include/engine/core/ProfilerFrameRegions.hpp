#pragma once

#include "engine/core/ProfilerCounters.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace kb::core {

// One region of a frame: every entry of the timeline with the same name under the same parent is
// one row, so a system that runs 300 times shows as one row with 300 calls.
struct ProfilerRegionRow {
    std::string name;
    std::uint32_t depth = 0U;
    std::uint64_t totalNanoseconds = 0U;
    // The part of `total` that no region inside it accounts for.
    std::uint64_t selfNanoseconds = 0U;
    std::uint32_t calls = 0U;
    // The part of `total` spent waiting: all of it for a waiting region, else what the waiting
    // regions inside it add up to.
    std::uint64_t waitNanoseconds = 0U;
    bool waiting = false;
};

struct ProfilerFrameRecord {
    std::uint64_t frame = 0U;
    // In tree order: a row is followed by its children.
    std::vector<ProfilerRegionRow> rows;

    // The time of the outermost region, which is the frame; zero when nothing was recorded.
    [[nodiscard]] std::uint64_t FrameNanoseconds() const noexcept { return rows.empty() ? 0U : rows.front().totalNanoseconds; }
};

// The regions of the last `HistoryFrames()` frames, read from the timeline of ProfilerCounters.
class ProfilerFrameRegions final {
public:
    [[nodiscard]] static constexpr std::size_t HistoryFrames() noexcept { return 240U; }

    // Turns the timeline of one frame into the rows of that frame and keeps them.
    void Capture(std::uint64_t frame, const std::vector<ProfilerTimelineEvent>& timeline);
    void Clear() noexcept { frames_.clear(); }

    [[nodiscard]] std::size_t Size() const noexcept { return frames_.size(); }
    [[nodiscard]] const ProfilerFrameRecord* Latest() const noexcept { return frames_.empty() ? nullptr : &frames_.back(); }
    // nullptr once the frame has left the history, or was never captured.
    [[nodiscard]] const ProfilerFrameRecord* Find(std::uint64_t frame) const noexcept;
    [[nodiscard]] std::uint64_t OldestFrame() const noexcept { return frames_.empty() ? 0U : frames_.front().frame; }

    [[nodiscard]] static std::vector<ProfilerRegionRow> Aggregate(const std::vector<ProfilerTimelineEvent>& timeline);

private:
    std::deque<ProfilerFrameRecord> frames_;
};

} // namespace kb::core
