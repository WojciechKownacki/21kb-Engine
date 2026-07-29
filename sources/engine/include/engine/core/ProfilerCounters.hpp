#pragma once

#include <chrono>
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

struct ProfilerTimelineEvent {
    std::string name;
    std::uint64_t timestampNanoseconds = 0U;
};

class ProfilerCounters final {
public:
    class ScopedRegion final {
    public:
        ScopedRegion(ProfilerCounters& profiler, std::string_view name) noexcept
            : profiler_(&profiler), name_(name) { profiler_->Scope(name_); }
        ~ScopedRegion() { if (profiler_ != nullptr) profiler_->Timeline(name_); }
        ScopedRegion(const ScopedRegion&) = delete;
        ScopedRegion& operator=(const ScopedRegion&) = delete;
        ScopedRegion(ScopedRegion&& other) noexcept : profiler_(std::exchange(other.profiler_, nullptr)), name_(other.name_) {}
    private:
        ProfilerCounters* profiler_;
        std::string_view name_;
    };

    [[nodiscard]] static constexpr bool Enabled() noexcept {
#if defined(NDEBUG)
        return false;
#else
        return true;
#endif
    }

    void Scope(std::string_view) noexcept { if (Enabled()) ++scopes; }
    [[nodiscard]] ScopedRegion BeginScope(std::string_view name) noexcept { return ScopedRegion{ *this, name }; }

    void Counter(std::string_view name, std::uint64_t value) {
        if (!Enabled()) return;
        for (ProfilerCounterSample& sample : counters_) {
            if (sample.name == name) { sample.value = value; return; }
        }
        counters_.push_back({ std::string{ name }, value });
    }

    void Timeline(std::string_view name) {
        if (!Enabled()) return;
        ++timelineEvents;
        timeline_.push_back({ std::string{ name }, NowNanoseconds() });
    }

    void Allocation() noexcept { if (Enabled()) ++allocations; }

    [[nodiscard]] const std::vector<ProfilerCounterSample>& Counters() const noexcept { return counters_; }
    [[nodiscard]] const std::vector<ProfilerTimelineEvent>& TimelineEvents() const noexcept { return timeline_; }

    std::uint64_t scopes = 0U;
    std::uint64_t timelineEvents = 0U;
    std::uint64_t allocations = 0U;

private:
    [[nodiscard]] static std::uint64_t NowNanoseconds() noexcept {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    std::vector<ProfilerCounterSample> counters_;
    std::vector<ProfilerTimelineEvent> timeline_;
};

} // namespace kb::core
