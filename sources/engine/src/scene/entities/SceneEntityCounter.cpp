#include "scene/entities/SceneEntityCounter.hpp"

namespace kb::scene {

std::size_t SceneEntityCounter::CountWithComponent(const kb::ecs::World& world, std::uint64_t componentId) noexcept {
    return world.NativeStorage().CountWithComponent(static_cast<kb::ecs::ComponentId>(componentId));
}

} // namespace kb::scene
