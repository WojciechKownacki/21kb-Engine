#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/ComponentEvent.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/TypeIds.hpp"
#include "engine/ecs/WorldConfig.hpp"
#include "engine/ecs/WorldInspection.hpp"
#include "engine/ecs/WorldSnapshot.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>

struct ecs_world_t;

namespace kb::ecs {

class ComponentRegistry;
class RelationTypeRegistry;
class QueryState;
class TagTypeRegistry;
template <typename... Components>
class Query;

class World {
public:
    explicit World(WorldConfig config = WorldConfig{});
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    World(World&& other) noexcept;
    World& operator=(World&& other) noexcept;

#include "engine/ecs/world/WorldEntityApi.inl"
#include "engine/ecs/world/WorldComponentApi.inl"
#include "engine/ecs/world/WorldQueryApi.inl"
#include "engine/ecs/world/WorldEventApi.inl"
#include "engine/ecs/world/WorldRelationApi.inl"
#include "engine/ecs/world/WorldSerializationApi.inl"
#include "engine/ecs/world/WorldRuntimeApi.inl"

private:
#include "engine/ecs/world/WorldPrivateApi.inl"

    ecs_world_t* world_ = nullptr;
    WorldConfig config_{};
    std::unique_ptr<ComponentRegistry> components_;
    std::unique_ptr<TagTypeRegistry> tags_;
    std::unique_ptr<RelationTypeRegistry> relations_;
};

} // namespace kb::ecs

#include "engine/ecs/World.inl"
