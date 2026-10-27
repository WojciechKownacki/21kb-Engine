#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::core {

struct ProfilerCounterSample {
    std::string name;
    std::uint64_t value = 0U;
};

// A timeline entry. `timestampNanoseconds` is the start of the region on the steady clock.
// A region recorded through BeginScope carries its wall-clock duration and nesting depth;
// an instant event recorded through Timeline has a duration of zero and the depth of the
// region that was open when it was recorded.
struct ProfilerTimelineEvent {
    std::string name;
    std::uint64_t timestampNanoseconds = 0U;
    std::uint64_t durationNanoseconds = 0U;
    std::uint32_t depth = 0U;
};

// Debug helper for counting scopes, allocations, named counters and timeline regions.
// The static default is on in debug builds and off in NDEBUG builds; a tool that wants
// measurements in a release build constructs it with `ProfilerCounters{ true }`.
// Recorded data is bounded: once `MaxTimelineEvents()` events or `MaxCounters()` distinct
// counter names are stored, further ones are counted in DroppedTimelineEvents() and
// DroppedCounters() instead of growing without limit.
class ProfilerCounters final {
public:
    class ScopedRegion final {
    public:
        ScopedRegion(ProfilerCounters& profiler, std::string_view name) noexcept
            : profiler_(&profiler), name_(name) {
            profiler_->Scope(name_);
            if (profiler_->active_) {
                start_ = NowNanoseconds();
                depth_ = profiler_->openRegions_++;
                tracked_ = true;
            }
        }
        // Runs during exception unwinding too, so every begun region is paired with its end.
        ~ScopedRegion() {
            if (profiler_ == nullptr) {
                return;
            }
            if (tracked_) {
                --profiler_->openRegions_;
                profiler_->Record(name_, start_, NowNanoseconds() - start_, depth_);
            } else {
                profiler_->Timeline(name_);
            }
        }
        ScopedRegion(const ScopedRegion&) = delete;
        ScopedRegion& operator=(const ScopedRegion&) = delete;
        ScopedRegion(ScopedRegion&& other) noexcept
            : profiler_(std::exchange(other.profiler_, nullptr)), name_(other.name_),
              start_(other.start_), depth_(other.depth_), tracked_(other.tracked_) {}
    private:
        ProfilerCounters* profiler_;
        std::string_view name_;
        std::uint64_t start_ = 0U;
        std::uint32_t depth_ = 0U;
        bool tracked_ = false;
    };

    ProfilerCounters() noexcept = default;
    explicit ProfilerCounters(bool active) noexcept : active_(active) {}

    [[nodiscard]] static constexpr bool Enabled() noexcept {
#if defined(NDEBUG)
        return false;
#else
        return true;
#endif
    }

    [[nodiscard]] static constexpr std::size_t MaxTimelineEvents() noexcept { return 4096U; }
    [[nodiscard]] static constexpr std::size_t MaxCounters() noexcept { return 256U; }

    [[nodiscard]] bool Active() const noexcept { return active_; }

    void Scope(std::string_view) noexcept { if (active_) ++scopes; }
    [[nodiscard]] ScopedRegion BeginScope(std::string_view name) noexcept { return ScopedRegion{ *this, name }; }

    void Counter(std::string_view name, std::uint64_t value) {
        if (!active_) return;
        for (ProfilerCounterSample& sample : counters_) {
            if (sample.name == name) { sample.value = value; return; }
        }
        if (counters_.size() >= MaxCounters()) { ++droppedCounters_; return; }
        counters_.push_back({ std::string{ name }, value });
    }

    void Timeline(std::string_view name) {
        if (!active_) return;
        Record(name, NowNanoseconds(), 0U, openRegions_);
    }

    void Allocation() noexcept { if (active_) ++allocations; }

    [[nodiscard]] const std::vector<ProfilerCounterSample>& Counters() const noexcept { return counters_; }
    [[nodiscard]] const std::vector<ProfilerTimelineEvent>& TimelineEvents() const noexcept { return timeline_; }
    // Number of regions begun and not yet ended; zero when every region is paired.
    [[nodiscard]] std::uint32_t OpenRegions() const noexcept { return openRegions_; }
    [[nodiscard]] std::uint64_t DroppedTimelineEvents() const noexcept { return droppedTimeline_; }
    [[nodiscard]] std::uint64_t DroppedCounters() const noexcept { return droppedCounters_; }

    std::uint64_t scopes = 0U;
    std::uint64_t timelineEvents = 0U;
    std::uint64_t allocations = 0U;

private:
    [[nodiscard]] static std::uint64_t NowNanoseconds() noexcept {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void Record(std::string_view name, std::uint64_t start, std::uint64_t duration, std::uint32_t depth) {
        ++timelineEvents;
        if (timeline_.size() >= MaxTimelineEvents()) { ++droppedTimeline_; return; }
        timeline_.push_back({ std::string{ name }, start, duration, depth });
    }

    bool active_ = Enabled();
    std::uint32_t openRegions_ = 0U;
    std::uint64_t droppedTimeline_ = 0U;
    std::uint64_t droppedCounters_ = 0U;
    std::vector<ProfilerCounterSample> counters_;
    std::vector<ProfilerTimelineEvent> timeline_;
};

} // namespace kb::core
