#include "engine/ecs/World.hpp"

#include <flecs.h>

#include <stdexcept>
#include <utility>

namespace kb::ecs {

World::World()
    : world_(ecs_init()) {
    if (world_ == nullptr) {
        throw std::runtime_error("Failed to initialize ECS world");
    }
}

World::~World() {
    Reset();
}

World::World(World&& other) noexcept
    : world_(std::exchange(other.world_, nullptr)) {}

World& World::operator=(World&& other) noexcept {
    if (this != &other) {
        Reset();
        world_ = std::exchange(other.world_, nullptr);
    }
    return *this;
}

Entity World::CreateEntity() {
    return Entity{ ecs_new(world_) };
}

Entity World::CreateEntity(std::string_view name) {
    Entity entity = CreateEntity();
    if (!name.empty()) {
        const std::string ownedName{ name };
        ecs_set_name(world_, entity.Id(), ownedName.c_str());
    }
    return entity;
}

void World::DestroyEntity(Entity entity) noexcept {
    if (world_ != nullptr && entity.IsValid() && ecs_is_valid(world_, entity.Id())) {
        ecs_delete(world_, entity.Id());
    }
}

bool World::IsAlive(Entity entity) const noexcept {
    return world_ != nullptr && entity.IsValid() && ecs_is_valid(world_, entity.Id()) && ecs_is_alive(world_, entity.Id());
}

std::string World::Name(Entity entity) const {
    if (!IsAlive(entity)) {
        return {};
    }

    if (const char* name = ecs_get_name(world_, entity.Id()); name != nullptr) {
        return std::string{ name };
    }

    return {};
}

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

void World::Reset() noexcept {
    if (world_ != nullptr) {
        ecs_fini(world_);
        world_ = nullptr;
    }
}

} // namespace kb::ecs
