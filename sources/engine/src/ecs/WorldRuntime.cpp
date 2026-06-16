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

const NativeArchetypeStorage& World::NativeStorage() const noexcept {
    return *nativeStorage_;
}

NativeEcsStorageStats World::NativeStorageStats() const {
    return nativeStorage_ != nullptr ? nativeStorage_->Stats() : NativeEcsStorageStats{};
}

std::size_t World::NativeChunkPayloadBytes() const noexcept {
    return nativeStorage_ != nullptr ? nativeStorage_->ChunkPayloadBytes() : 0;
}

} // namespace kb::ecs
