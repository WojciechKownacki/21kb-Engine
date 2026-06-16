#pragma once

#include "engine/ecs/SystemAccess.hpp"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace kb::ecs {

class World;

struct SystemProfilerCounters {
    std::uint64_t entitiesProcessed = 0;
    std::uint64_t bytesTouched = 0;
};

class System {
public:
    virtual ~System() = default;

    [[nodiscard]] virtual std::string_view Name() const noexcept;
    [[nodiscard]] virtual SystemAccess DeclareAccess(World& world) const = 0;
    [[nodiscard]] SystemProfilerCounters ProfilerCounters() const noexcept;
    void ResetProfilerCounters() noexcept;

    virtual void OnCreate(World& world);
    virtual void OnUpdate(World& world, float deltaSeconds);
    virtual void OnDestroy(World& world);

protected:
    void ReportProfilerWork(std::uint64_t entitiesProcessed, std::uint64_t bytesTouched) noexcept;

private:
    std::atomic<std::uint64_t> profilerEntitiesProcessed_{ 0 };
    std::atomic<std::uint64_t> profilerBytesTouched_{ 0 };
};

} // namespace kb::ecs
