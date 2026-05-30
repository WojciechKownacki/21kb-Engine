#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/WorldSnapshot.hpp"

#include <cstddef>
#include <unordered_map>

struct ecs_world_t;

namespace kb::ecs {

class SnapshotEntityIndex {
public:
    explicit SnapshotEntityIndex(WorldSnapshot& snapshot);

    [[nodiscard]] EntitySnapshot& FindOrAdd(ecs_world_t* world, Entity entity);

private:
    [[nodiscard]] static std::string ReadName(ecs_world_t* world, Entity entity);

    WorldSnapshot& snapshot_;
    std::unordered_map<Entity::IdType, std::size_t> entityIndices_;
};

} // namespace kb::ecs
