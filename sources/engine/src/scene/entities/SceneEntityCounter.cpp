#include "scene/entities/SceneEntityCounter.hpp"

#include <flecs.h>

#include <array>

namespace kb::scene {

std::size_t SceneEntityCounter::CountWithComponent(const kb::ecs::World& world, std::uint64_t componentId) noexcept {
    const int backendCount = ecs_count_id(world.NativeHandle(), componentId);
    if (backendCount > 0) {
        return static_cast<std::size_t>(backendCount);
    }

    const std::array componentIds{ static_cast<kb::ecs::ComponentId>(componentId) };
    std::size_t count = 0;
    for (const kb::ecs::NativeArchetypeMatch& match : world.NativeStorage().MatchingArchetypes(componentIds)) {
        count += match.liveEntities;
    }
    return count;
}

} // namespace kb::scene
