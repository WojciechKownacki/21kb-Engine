#include "ecs/snapshot/SnapshotEntityIndex.hpp"

#include <flecs.h>

#include <string>

namespace kb::ecs {

SnapshotEntityIndex::SnapshotEntityIndex(WorldSnapshot& snapshot)
    : snapshot_(snapshot) {}

EntitySnapshot& SnapshotEntityIndex::FindOrAdd(ecs_world_t* world, Entity entity) {
    const auto [it, inserted] = entityIndices_.try_emplace(entity.Id(), snapshot_.entities.size());
    if (inserted) {
        snapshot_.entities.push_back(EntitySnapshot{
            .id = entity.Id(),
            .name = ReadName(world, entity),
            .components = {},
        });
    }
    return snapshot_.entities[it->second];
}

std::string SnapshotEntityIndex::ReadName(ecs_world_t* world, Entity entity) {
    const char* name = ecs_get_name(world, entity.Id());
    return name == nullptr ? std::string{} : std::string{ name };
}

} // namespace kb::ecs
