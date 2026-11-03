#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::core {

// What a counter measures; a tool measures an area only while somebody wants its counters.
enum class ProfilerArea : std::uint8_t { Cpu, Gpu, Render, Ecs, Memory };

enum class ProfilerUnit : std::uint8_t { Milliseconds, FramesPerSecond, Count, Megabytes, Percent };

enum class ProfilerSeverity : std::uint8_t { Normal, Warning, Alarm };

// A counter's identity and thresholds. `budget` and `alarm` are the values at which a reading
// turns Warning and Alarm; zero means the counter has no threshold. Most counters get worse as
// they grow (milliseconds, draw calls); `lowerIsWorse` flips that for frame rate.
struct ProfilerCounterInfo {
    std::string_view id;          // stable key, persisted by tools
    std::string_view label;       // English name
    std::string_view shortLabel;  // name in a compact list
    std::string_view group;       // English group heading
    ProfilerArea area = ProfilerArea::Cpu;
    ProfilerUnit unit = ProfilerUnit::Count;
    double budget = 0.0;
    double alarm = 0.0;
    bool lowerIsWorse = false;
};

// The counters every tool knows by name; their position in the registry is the enumerator value.
enum class ProfilerBuiltIn : std::uint8_t {
    FramesPerSecond, FrameTime, RenderThreadTime, GpuTime,
    DrawCalls, Triangles, ComputeDispatches, Views,
    ProcessMemory, GpuMemory, TextureMemory, RenderTargetMemory,
    EcsEntities, EcsChunks, EcsPayload, EcsEntityRecords, EcsOccupancy, EcsStorageAllocations,
    EcsQueryCacheHits, EcsQueryCacheMisses, EcsQueryPrepare, EcsQueryKernels, EcsQueryTraffic,
    Count
};

struct ProfilerCounterStats {
    bool available = false;  // false until the first Publish
    double now = 0.0;
    double average = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    double p95 = 0.0;
    std::size_t samples = 0U;
};

// The measured values of one process: a table of built-in counters, up to `MaxCustom()` counters
// added at run time, and the last `HistoryFrames()` readings of each. It knows nothing about who
// publishes or draws them.
class ProfilerRegistry final {
public:
    [[nodiscard]] static constexpr std::size_t HistoryFrames() noexcept { return 240U; }
    [[nodiscard]] static constexpr std::size_t MaxCustom() noexcept { return 32U; }

    ProfilerRegistry();

    // Built-in counters come first, in table order, custom ones follow in registration order.
    [[nodiscard]] std::size_t CounterCount() const noexcept { return entries_.size(); }
    [[nodiscard]] const ProfilerCounterInfo& Info(std::size_t counter) const;
    [[nodiscard]] std::optional<std::size_t> Find(std::string_view id) const noexcept;

    // Adds a counter named `name`, or returns the one already registered under it. nullopt once
    // MaxCustom() counters exist; DroppedCustom() counts those refusals.
    [[nodiscard]] std::optional<std::size_t> RegisterCustom(
        std::string_view name, ProfilerArea area, ProfilerUnit unit);
    [[nodiscard]] std::size_t DroppedCustom() const noexcept { return droppedCustom_; }

    [[nodiscard]] static constexpr std::size_t Id(ProfilerBuiltIn counter) noexcept {
        return static_cast<std::size_t>(counter);
    }
    void Publish(std::size_t counter, double value);
    [[nodiscard]] ProfilerCounterStats Stats(std::size_t counter) const;
    // Oldest to newest; returns the number of readings written, at most out.size().
    [[nodiscard]] std::size_t Samples(std::size_t counter, std::span<double> out) const;

    [[nodiscard]] static ProfilerSeverity Severity(const ProfilerCounterInfo& info, double value) noexcept;

private:
    struct Entry {
        ProfilerCounterInfo info;
        std::string customId;
        std::array<double, 240U> ring{};
        std::size_t head = 0U;  // where the next reading goes
        std::size_t count = 0U;
    };

    std::vector<Entry> entries_;  // reserved for every counter, so views into customId stay valid
    std::size_t builtInCount_ = 0U;
    std::size_t droppedCustom_ = 0U;
};

} // namespace kb::core
