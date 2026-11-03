#include "engine/core/ProfilerRecorder.hpp"

#include <cmath>
#include <format>
#include <limits>
#include <string_view>
#include <utility>

namespace kb::core {

void ProfilerRecorder::Start(const ProfilerRegistry& registry, std::vector<std::size_t> counters) {
    Clear();
    counters_ = std::move(counters);
    for (const std::size_t counter : counters_) seenPublishes_.push_back(registry.PublishCount(counter));
    recording_ = true;
}

void ProfilerRecorder::Clear() noexcept {
    counters_.clear();
    seenPublishes_.clear();
    rows_.clear();
    recording_ = false;
    full_ = false;
}

void ProfilerRecorder::Capture(const ProfilerRegistry& registry, std::uint64_t frame, double milliseconds) {
    if (!recording_) return;
    if (rows_.size() >= MaxFrames()) {
        recording_ = false;
        full_ = true;
        return;
    }
    Row row{ .frame = frame, .milliseconds = milliseconds, .values = std::vector<double>(counters_.size(), std::numeric_limits<double>::quiet_NaN()) };
    for (std::size_t column = 0U; column < counters_.size(); ++column) {
        const std::uint64_t published = registry.PublishCount(counters_[column]);
        if (published == seenPublishes_[column]) continue;
        seenPublishes_[column] = published;
        row.values[column] = registry.Stats(counters_[column]).now;
    }
    rows_.push_back(std::move(row));
}

// A field with a comma or a quote in it (the name of a system) is quoted, as CSV readers expect.
std::string ProfilerRecorder::CsvField(std::string_view text) {
    if (text.find_first_of(",\"\n") == std::string_view::npos) return std::string{ text };
    std::string quoted = "\"";
    for (const char c : text) quoted += c == '"' ? std::string{ "\"\"" } : std::string(1U, c);
    return quoted + '"';
}

std::string ProfilerRecorder::ToCsv(const ProfilerRegistry& registry) const {
    std::string csv = "frame,time_ms";
    for (const std::size_t counter : counters_) csv += ',' + CsvField(registry.Info(counter).id);
    csv += '\n';
    for (const Row& row : rows_) {
        csv += std::format("{},{:.3f}", row.frame, row.milliseconds);
        for (const double value : row.values) {
            csv += ',';
            if (!std::isnan(value)) csv += std::format("{:.4f}", value);
        }
        csv += '\n';
    }
    return csv;
}

} // namespace kb::core
