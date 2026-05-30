#pragma once

#include "engine/ecs/System.hpp"
#include "engine/ecs/World.hpp"

#include <memory>
#include <vector>

namespace kb::ecs {

class SystemScheduler {
public:
    SystemScheduler() = default;
    ~SystemScheduler();

    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;
    SystemScheduler(SystemScheduler&&) noexcept = default;
    SystemScheduler& operator=(SystemScheduler&&) noexcept = default;

    void Add(std::unique_ptr<System> system, World& world);
    void Update(World& world, float deltaSeconds);
    void Shutdown(World& world) noexcept;

private:
    std::vector<std::unique_ptr<System>> systems_;
};

} // namespace kb::ecs
