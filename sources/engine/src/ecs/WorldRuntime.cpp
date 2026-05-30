#include "engine/ecs/World.hpp"

#include <flecs.h>

namespace kb::ecs {

bool World::Progress(float deltaSeconds) {
    return world_ != nullptr && ecs_progress(world_, deltaSeconds);
}

void World::RequestQuit() noexcept {
    if (world_ != nullptr) {
        ecs_quit(world_);
    }
}

bool World::ShouldQuit() const noexcept {
    return world_ == nullptr || ecs_should_quit(world_);
}

ecs_world_t* World::NativeHandle() noexcept {
    return world_;
}

const ecs_world_t* World::NativeHandle() const noexcept {
    return world_;
}

} // namespace kb::ecs
