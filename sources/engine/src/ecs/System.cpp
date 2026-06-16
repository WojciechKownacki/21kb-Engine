#include "engine/ecs/System.hpp"

#include <typeinfo>

namespace kb::ecs {

std::string_view System::Name() const noexcept {
    return typeid(*this).name();
}

SystemProfilerCounters System::ProfilerCounters() const noexcept {
    return SystemProfilerCounters{
        .entitiesProcessed = profilerEntitiesProcessed_.load(std::memory_order_relaxed),
        .bytesTouched = profilerBytesTouched_.load(std::memory_order_relaxed),
    };
}

void System::ResetProfilerCounters() noexcept {
    profilerEntitiesProcessed_.store(0, std::memory_order_relaxed);
    profilerBytesTouched_.store(0, std::memory_order_relaxed);
}

void System::OnCreate(World& world) {
    static_cast<void>(world);
}

void System::OnUpdate(World& world, float deltaSeconds) {
    static_cast<void>(world);
    static_cast<void>(deltaSeconds);
}

void System::OnDestroy(World& world) {
    static_cast<void>(world);
}

void System::ReportProfilerWork(std::uint64_t entitiesProcessed, std::uint64_t bytesTouched) noexcept {
    profilerEntitiesProcessed_.fetch_add(entitiesProcessed, std::memory_order_relaxed);
    profilerBytesTouched_.fetch_add(bytesTouched, std::memory_order_relaxed);
}

} // namespace kb::ecs
