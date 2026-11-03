#include "engine/core/ProfilerRegistry.hpp"

#include <algorithm>
#include <cmath>

namespace kb::core {
namespace {

using Area = ProfilerArea;
using Unit = ProfilerUnit;

constexpr ProfilerCounterInfo C(std::string_view id, std::string_view label, std::string_view shortLabel,
    std::string_view group, Area area, Unit unit, double budget = 0.0, double alarm = 0.0,
    bool lowerIsWorse = false) {
    return ProfilerCounterInfo{ id, label, shortLabel, group, area, unit, budget, alarm, lowerIsWorse };
}

// The frame budgets are the 60 and 30 frames per second targets (16.6 and 33.3 ms).
constexpr std::array<ProfilerCounterInfo, static_cast<std::size_t>(ProfilerBuiltIn::Count)> kBuiltIn{{
    C("fps", "FPS", "FPS", "Frame", Area::Cpu, Unit::FramesPerSecond, 60.0, 30.0, true),
    C("frame_ms", "Frame time", "Frame", "Frame", Area::Cpu, Unit::Milliseconds, 16.6, 33.3),
    C("render_thread_ms", "Render thread", "Render", "Frame", Area::Cpu, Unit::Milliseconds, 16.6, 33.3),
    C("gpu_ms", "GPU", "GPU", "Frame", Area::Gpu, Unit::Milliseconds, 16.6, 33.3),
    C("draw_calls", "Draw calls", "Draws", "Drawing", Area::Render, Unit::Count, 2000.0, 5000.0),
    C("triangles", "Triangles", "Tris", "Drawing", Area::Render, Unit::Count, 2'000'000.0, 5'000'000.0),
    C("compute_dispatches", "Compute dispatches", "Compute", "Drawing", Area::Render, Unit::Count),
    C("views", "Render views", "Views", "Drawing", Area::Render, Unit::Count),
    C("process_memory_mb", "Process memory", "Mem", "Memory", Area::Memory, Unit::Megabytes, 3072.0, 6144.0),
    C("gpu_memory_mb", "GPU memory", "VRAM", "Memory", Area::Memory, Unit::Megabytes),
    C("texture_memory_mb", "Textures", "Textures", "Memory", Area::Memory, Unit::Megabytes),
    C("render_target_memory_mb", "Render targets", "Targets", "Memory", Area::Memory, Unit::Megabytes),
    C("ecs_entities", "Entities", "Entities", "ECS", Area::Ecs, Unit::Count),
    C("ecs_chunks", "Chunks", "Chunks", "ECS", Area::Ecs, Unit::Count),
    C("ecs_payload_mb", "Native payload", "Payload", "ECS", Area::Ecs, Unit::Megabytes),
    C("ecs_entity_records_mb", "Entity records", "Records", "ECS", Area::Ecs, Unit::Megabytes),
    C("ecs_occupancy", "Chunk occupancy", "Occupancy", "ECS", Area::Ecs, Unit::Percent),
    C("ecs_storage_allocations", "Storage allocations", "Allocs", "ECS", Area::Ecs, Unit::Count),
    C("ecs_query_cache_hits", "Query cache hits", "Q hits", "ECS", Area::Ecs, Unit::Count),
    C("ecs_query_cache_misses", "Query cache misses", "Q misses", "ECS", Area::Ecs, Unit::Count),
    C("ecs_query_prepare_ms", "Query prepare", "Q prepare", "ECS", Area::Ecs, Unit::Milliseconds),
    C("ecs_query_kernels_ms", "Query kernels", "Q kernels", "ECS", Area::Ecs, Unit::Milliseconds),
    C("ecs_query_traffic_mb", "Query traffic", "Q traffic", "ECS", Area::Ecs, Unit::Megabytes),
}};

static_assert(kBuiltIn.size() + ProfilerRegistry::MaxCustom() <= 64U, "counter selections are 64-bit masks");

} // namespace

ProfilerRegistry::ProfilerRegistry() {
    entries_.reserve(kBuiltIn.size() + MaxCustom());
    for (const ProfilerCounterInfo& info : kBuiltIn) {
        entries_.push_back(Entry{ .info = info });
    }
    builtInCount_ = entries_.size();
}

const ProfilerCounterInfo& ProfilerRegistry::Info(std::size_t counter) const {
    return entries_.at(counter).info;
}

std::optional<std::size_t> ProfilerRegistry::Find(std::string_view id) const noexcept {
    for (std::size_t index = 0U; index < entries_.size(); ++index) {
        if (entries_[index].info.id == id) return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> ProfilerRegistry::RegisterCustom(
    std::string_view name, ProfilerArea area, ProfilerUnit unit) {
    if (const std::optional<std::size_t> existing = Find(name)) return existing;
    if (entries_.size() - builtInCount_ >= MaxCustom()) {
        ++droppedCustom_;
        return std::nullopt;
    }
    Entry& entry = entries_.emplace_back();
    entry.customId = std::string{ name };
    entry.info = ProfilerCounterInfo{ entry.customId, entry.customId, entry.customId, "Custom", area, unit };
    return entries_.size() - 1U;
}

void ProfilerRegistry::Publish(std::size_t counter, double value) {
    Entry& entry = entries_.at(counter);
    entry.ring[entry.head] = value;
    entry.head = (entry.head + 1U) % entry.ring.size();
    entry.count = std::min(entry.count + 1U, entry.ring.size());
}

std::size_t ProfilerRegistry::Samples(std::size_t counter, std::span<double> out) const {
    const Entry& entry = entries_.at(counter);
    const std::size_t written = std::min(entry.count, out.size());
    // The newest `written` readings, oldest first.
    for (std::size_t index = 0U; index < written; ++index) {
        const std::size_t back = written - index;  // 1 = newest
        out[index] = entry.ring[(entry.head + entry.ring.size() - back) % entry.ring.size()];
    }
    return written;
}

ProfilerCounterStats ProfilerRegistry::Stats(std::size_t counter) const {
    const Entry& entry = entries_.at(counter);
    ProfilerCounterStats stats;
    if (entry.count == 0U) return stats;
    std::array<double, HistoryFrames()> values{};
    const std::size_t count = Samples(counter, values);
    double sum = 0.0;
    for (std::size_t index = 0U; index < count; ++index) sum += values[index];
    stats.available = true;
    stats.samples = count;
    stats.now = values[count - 1U];
    stats.average = sum / static_cast<double>(count);
    std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
    stats.minimum = values[0];
    stats.maximum = values[count - 1U];
    // Nearest rank: the smallest reading that at least 95% of the readings do not exceed.
    stats.p95 = values[static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(count))) - 1U];
    return stats;
}

ProfilerSeverity ProfilerRegistry::Severity(const ProfilerCounterInfo& info, double value) noexcept {
    const auto reached = [&](double limit) {
        return limit > 0.0 && (info.lowerIsWorse ? value < limit : value > limit);
    };
    if (reached(info.alarm)) return ProfilerSeverity::Alarm;
    return reached(info.budget) ? ProfilerSeverity::Warning : ProfilerSeverity::Normal;
}

} // namespace kb::core
