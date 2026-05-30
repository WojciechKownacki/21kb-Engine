#include "engine/ecs/SystemScheduler.hpp"

#include <utility>

namespace kb::ecs {

SystemScheduler::~SystemScheduler() = default;

void SystemScheduler::Add(std::unique_ptr<System> system, World& world) {
    if (system == nullptr) {
        return;
    }

    system->OnCreate(world);
    systems_.push_back(std::move(system));
}

void SystemScheduler::Update(World& world, float deltaSeconds) {
    for (const auto& system : systems_) {
        system->OnUpdate(world, deltaSeconds);
    }
}

void SystemScheduler::Shutdown(World& world) noexcept {
    for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) {
        try {
            (*it)->OnDestroy(world);
        } catch (...) {
        }
    }
    systems_.clear();
}

} // namespace kb::ecs
