#pragma once

#include "engine/core/ProfilerRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kb::core {

// Records the counters frame by frame and writes them as CSV: a header line `frame,time_ms,<counter
// ids>`, then one line per frame, like the per-frame files of scripts/profile_scene.py. A counter
// that had no new reading in a frame leaves its cell empty.
class ProfilerRecorder final {
public:
    // Frames kept; a recording that reaches it stops and says so.
    [[nodiscard]] static constexpr std::size_t MaxFrames() noexcept { return 36000U; }

    void Start(const ProfilerRegistry& registry, std::vector<std::size_t> counters);
    void Stop() noexcept { recording_ = false; }
    void Clear() noexcept;

    // Adds one frame: the counters that published since the previous frame, `milliseconds` after the start.
    void Capture(const ProfilerRegistry& registry, std::uint64_t frame, double milliseconds);

    [[nodiscard]] bool Recording() const noexcept { return recording_; }
    // True once the recording stopped because it was full.
    [[nodiscard]] bool Full() const noexcept { return full_; }
    [[nodiscard]] std::size_t Frames() const noexcept { return rows_.size(); }
    [[nodiscard]] std::uint64_t FirstFrame() const noexcept { return rows_.empty() ? 0U : rows_.front().frame; }
    [[nodiscard]] std::uint64_t LastFrame() const noexcept { return rows_.empty() ? 0U : rows_.back().frame; }

    [[nodiscard]] std::string ToCsv(const ProfilerRegistry& registry) const;

    // One CSV field: quoted when it holds a comma, a quote or a line break.
    [[nodiscard]] static std::string CsvField(std::string_view text);

private:
    struct Row {
        std::uint64_t frame = 0U;
        double milliseconds = 0.0;
        std::vector<double> values;  // NaN where the counter had no new reading
    };

    std::vector<std::size_t> counters_;
    std::vector<std::uint64_t> seenPublishes_;
    std::vector<Row> rows_;
    bool recording_ = false;
    bool full_ = false;
};

} // namespace kb::core
