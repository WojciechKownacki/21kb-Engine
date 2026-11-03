#include "engine/core/ProfilerRegistry.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using kb::core::ProfilerArea;
using kb::core::ProfilerBuiltIn;
using kb::core::ProfilerRegistry;
using kb::core::ProfilerSeverity;
using kb::core::ProfilerUnit;

void Require(bool value, const char* error) {
    if (!value) throw std::runtime_error(error);
}

bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }

void StatsOfKnownData() {
    ProfilerRegistry registry;
    const std::size_t frame = ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime);
    Require(!registry.Stats(frame).available, "A counter nobody published must be unavailable");
    for (int value = 1; value <= 100; ++value) registry.Publish(frame, value);
    const auto stats = registry.Stats(frame);
    Require(stats.available && stats.samples == 100U, "Hundred readings expected");
    Require(Near(stats.now, 100.0) && Near(stats.minimum, 1.0) && Near(stats.maximum, 100.0), "Now, minimum, maximum");
    Require(Near(stats.average, 50.5), "Average of 1..100 is 50.5");
    Require(Near(stats.p95, 95.0), "Nearest-rank p95 of 1..100 is 95");
}

void PercentileIsOrderIndependent() {
    ProfilerRegistry registry;
    const std::size_t draws = ProfilerRegistry::Id(ProfilerBuiltIn::DrawCalls);
    for (double value : { 9.0, 1.0, 7.0, 3.0, 5.0 }) registry.Publish(draws, value);
    const auto stats = registry.Stats(draws);
    Require(Near(stats.p95, 9.0) && Near(stats.minimum, 1.0) && Near(stats.now, 5.0), "Unsorted readings");
}

void RingWrapsAndKeepsNewest() {
    ProfilerRegistry registry;
    const std::size_t frame = ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime);
    const std::size_t total = ProfilerRegistry::HistoryFrames() + 10U;
    for (std::size_t value = 1U; value <= total; ++value) registry.Publish(frame, static_cast<double>(value));
    std::array<double, ProfilerRegistry::HistoryFrames()> samples{};
    const std::size_t written = registry.Samples(frame, samples);
    Require(written == ProfilerRegistry::HistoryFrames(), "The ring holds exactly its capacity");
    Require(Near(samples.front(), 11.0) && Near(samples.back(), static_cast<double>(total)), "Oldest 10 readings are overwritten, order kept");
    const auto stats = registry.Stats(frame);
    Require(stats.samples == written && Near(stats.minimum, 11.0), "Statistics cover only what the ring holds");
    std::array<double, 3U> tail{};
    Require(registry.Samples(frame, tail) == 3U && Near(tail[2], static_cast<double>(total)) && Near(tail[0], static_cast<double>(total - 2U)),
        "A short buffer receives the newest readings");
}

void CustomCountersAreBounded() {
    ProfilerRegistry registry;
    const std::size_t builtIn = registry.CounterCount();
    const auto first = registry.RegisterCustom("enemies", ProfilerArea::Ecs, ProfilerUnit::Count);
    Require(first && *first == builtIn && registry.Info(*first).id == "enemies" && registry.Info(*first).group == "Custom", "First custom counter follows the built-in ones");
    Require(registry.RegisterCustom("enemies", ProfilerArea::Ecs, ProfilerUnit::Count) == first, "A name registers once");
    registry.Publish(*first, 38.0);
    Require(Near(registry.Stats(*first).now, 38.0) && registry.Find("enemies") == first, "A custom counter is published and found like a built-in one");
    for (std::size_t index = 1U; index < ProfilerRegistry::MaxCustom(); ++index) {
        Require(registry.RegisterCustom("c" + std::to_string(index), ProfilerArea::Cpu, ProfilerUnit::Milliseconds).has_value(), "Custom counters up to the limit");
    }
    Require(!registry.RegisterCustom("one too many", ProfilerArea::Cpu, ProfilerUnit::Count) && registry.DroppedCustom() == 1U, "The 33rd custom counter is refused and counted");
    Require(registry.Info(*first).id == "enemies" && registry.Info(registry.CounterCount() - 1U).id == "c31", "Views into a custom name survive later registrations");
}

void SeverityFollowsThresholds() {
    ProfilerRegistry registry;
    const auto& frame = registry.Info(ProfilerRegistry::Id(ProfilerBuiltIn::FrameTime));
    Require(ProfilerRegistry::Severity(frame, 16.0) == ProfilerSeverity::Normal, "Under the budget");
    Require(ProfilerRegistry::Severity(frame, 16.7) == ProfilerSeverity::Warning, "Over the 16.6 ms budget");
    Require(ProfilerRegistry::Severity(frame, 40.0) == ProfilerSeverity::Alarm, "Over the 33.3 ms alarm");
    const auto& fps = registry.Info(ProfilerRegistry::Id(ProfilerBuiltIn::FramesPerSecond));
    Require(ProfilerRegistry::Severity(fps, 61.0) == ProfilerSeverity::Normal && ProfilerRegistry::Severity(fps, 45.0) == ProfilerSeverity::Warning
        && ProfilerRegistry::Severity(fps, 20.0) == ProfilerSeverity::Alarm, "Frame rate gets worse as it falls");
    const auto& compute = registry.Info(ProfilerRegistry::Id(ProfilerBuiltIn::ComputeDispatches));
    Require(ProfilerRegistry::Severity(compute, 1e9) == ProfilerSeverity::Normal, "A counter without thresholds stays normal");
}

void BuiltInsAreConsistent() {
    ProfilerRegistry registry;
    Require(registry.CounterCount() == static_cast<std::size_t>(ProfilerBuiltIn::Count), "One table row per enumerator");
    Require(registry.Info(ProfilerRegistry::Id(ProfilerBuiltIn::GpuTime)).id == "gpu_ms" && registry.Find("ecs_query_traffic_mb") == ProfilerRegistry::Id(ProfilerBuiltIn::EcsQueryTraffic), "Enumerators match the table");
    for (std::size_t index = 0U; index < registry.CounterCount(); ++index) {
        Require(registry.Find(registry.Info(index).id) == index, "Counter ids are unique");
    }
}

} // namespace

int main() {
    try {
        StatsOfKnownData();
        PercentileIsOrderIndependent();
        RingWrapsAndKeepsNewest();
        CustomCountersAreBounded();
        SeverityFollowsThresholds();
        BuiltInsAreConsistent();
    } catch (const std::exception& error) {
        std::cerr << "Profiler registry test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Profiler registry tests passed\n";
    return 0;
}
