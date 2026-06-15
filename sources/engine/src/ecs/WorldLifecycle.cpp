#include "engine/ecs/World.hpp"

#include "ecs/world/WorldRegistrySet.hpp"

#include <flecs.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace kb::ecs {

World::World(WorldConfig config)
    : world_(ecs_init())
    , config_(config)
    , registries_(std::make_unique<WorldRegistrySet>())
    , nativeStorage_(std::make_unique<NativeArchetypeStorage>(config)) {
    if (world_ == nullptr) {
        throw std::runtime_error("Failed to initialize ECS world");
    }
}

World::~World() {
    Reset();
}

World::World(World&& other) noexcept
    : world_(std::exchange(other.world_, nullptr))
    , config_(other.config_)
    , registries_(std::move(other.registries_))
    , nativeStorage_(std::move(other.nativeStorage_))
    , nextEntityId_(std::exchange(other.nextEntityId_, 1'000'000)) {}

World& World::operator=(World&& other) noexcept {
    if (this != &other) {
        Reset();
        world_ = std::exchange(other.world_, nullptr);
        config_ = other.config_;
        registries_ = std::move(other.registries_);
        nativeStorage_ = std::move(other.nativeStorage_);
        nextEntityId_ = std::exchange(other.nextEntityId_, 1'000'000);
    }
    return *this;
}

void World::Reset() noexcept {
    if (world_ != nullptr) {
        ecs_fini(world_);
        world_ = nullptr;
    }
    if (registries_ != nullptr) {
        registries_->Clear();
    }
    nativeStorage_.reset();
}

const WorldConfig& World::Config() const noexcept {
    return config_;
}

} // namespace kb::ecs
