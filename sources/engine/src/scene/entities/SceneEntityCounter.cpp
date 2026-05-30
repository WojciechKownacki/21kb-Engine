#include "scene/entities/SceneEntityCounter.hpp"

#include <flecs.h>

#include <algorithm>

namespace kb::scene {

std::size_t SceneEntityCounter::CountWithComponent(const kb::ecs::World& world, std::uint64_t componentId) noexcept {
    return static_cast<std::size_t>(std::max(0, ecs_count_id(world.NativeHandle(), componentId)));
}

} // namespace kb::scene
