#include "engine/ecs/World.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/type/RelationTypeRegistry.hpp"
#include "ecs/type/TagTypeRegistry.hpp"

#include <flecs.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace kb::ecs {

World::World(WorldConfig config)
    : world_(ecs_init())
    , config_(config)
    , components_(std::make_unique<ComponentRegistry>())
    , tags_(std::make_unique<TagTypeRegistry>())
    , relations_(std::make_unique<RelationTypeRegistry>()) {
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
    , components_(std::move(other.components_))
    , tags_(std::move(other.tags_))
    , relations_(std::move(other.relations_)) {}

World& World::operator=(World&& other) noexcept {
    if (this != &other) {
        Reset();
        world_ = std::exchange(other.world_, nullptr);
        config_ = other.config_;
        components_ = std::move(other.components_);
        tags_ = std::move(other.tags_);
        relations_ = std::move(other.relations_);
    }
    return *this;
}

void World::Reset() noexcept {
    if (world_ != nullptr) {
        ecs_fini(world_);
        world_ = nullptr;
    }
    if (components_ != nullptr) {
        components_->Clear();
    }
    if (tags_ != nullptr) {
        tags_->Clear();
    }
    if (relations_ != nullptr) {
        relations_->Clear();
    }
}

const WorldConfig& World::Config() const noexcept {
    return config_;
}

} // namespace kb::ecs
