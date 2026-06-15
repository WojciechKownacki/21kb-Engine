#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/ComponentEvent.hpp"
#include "engine/ecs/ComponentReflection.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/QueryFilter.hpp"
#include "engine/ecs/TypeIds.hpp"
#include "engine/ecs/WorldConfig.hpp"
#include "engine/ecs/WorldEditorInspection.hpp"
#include "engine/ecs/WorldInspection.hpp"
#include "engine/ecs/WorldSerialization.hpp"
#include "engine/ecs/WorldSnapshot.hpp"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

struct ecs_world_t;
struct ecs_table_t;

namespace kb::ecs {

class QueryState;
class QueryPlanCache;
class WorldInternalAccess;
class WorldRegistrySet;
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
    friend class WorldInternalAccess;

#include "engine/ecs/world/WorldPrivateComponentApi.inl"
#include "engine/ecs/world/WorldPrivateQueryApi.inl"
#include "engine/ecs/world/WorldPrivateEventApi.inl"
#include "engine/ecs/world/WorldPrivateTypeApi.inl"
#include "engine/ecs/world/WorldPrivateLifecycleApi.inl"

    ecs_world_t* world_ = nullptr;
    WorldConfig config_{};
    std::unique_ptr<WorldRegistrySet> registries_;
    std::unique_ptr<QueryPlanCache> queryPlanCache_;
};

} // namespace kb::ecs

#include "engine/ecs/World.inl"
