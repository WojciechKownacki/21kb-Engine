#include "engine/ecs/World.hpp"

#include "engine/ecs/MutableComponentBorrowLocks.hpp"
#include "engine/ecs/StructuralChangeValidator.hpp"
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
    , nativeStorage_(std::make_unique<NativeArchetypeStorage>(config))
    , mutableComponentBorrowLocks_(std::make_unique<MutableComponentBorrowLocks>())
    , structuralChangeValidator_(std::make_unique<StructuralChangeValidator>()) {
    if (world_ == nullptr) {
        throw std::runtime_error("Failed to initialize ECS world");
    }
    queryPlanCache_.reserve(config_.reserveQueryCache);
}

World::~World() {
    Reset();
}

World::World(World&& other) noexcept
    : world_(std::exchange(other.world_, nullptr))
    , config_(other.config_)
    , registries_(std::move(other.registries_))
    , nativeStorage_(std::move(other.nativeStorage_))
    , mutableComponentBorrowLocks_(std::move(other.mutableComponentBorrowLocks_))
    , structuralChangeValidator_(std::move(other.structuralChangeValidator_))
    , telemetryCounters_(other.telemetryCounters_)
    , queryPlanCache_(std::move(other.queryPlanCache_)) {}

World& World::operator=(World&& other) noexcept {
    if (this != &other) {
        Reset();
        world_ = std::exchange(other.world_, nullptr);
        config_ = other.config_;
        registries_ = std::move(other.registries_);
        nativeStorage_ = std::move(other.nativeStorage_);
        mutableComponentBorrowLocks_ = std::move(other.mutableComponentBorrowLocks_);
        structuralChangeValidator_ = std::move(other.structuralChangeValidator_);
        telemetryCounters_ = other.telemetryCounters_;
        queryPlanCache_ = std::move(other.queryPlanCache_);
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
    if (mutableComponentBorrowLocks_ != nullptr) {
        mutableComponentBorrowLocks_->Clear();
    }
    queryPlanCache_.clear();
    structuralChangeValidator_.reset();
    nativeStorage_.reset();
}

const WorldConfig& World::Config() const noexcept {
    return config_;
}

} // namespace kb::ecs
