#include "ecs/snapshot/SnapshotEntityIndex.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

#include <string>

namespace kb::ecs {

SnapshotEntityIndex::SnapshotEntityIndex(WorldSnapshot& snapshot, EntityResolver resolver, void* resolverContext)
    : snapshot_(snapshot)
    , resolver_(resolver)
    , resolverContext_(resolverContext) {}

EntitySnapshot& SnapshotEntityIndex::FindOrAdd(ecs_world_t* world, Entity entity) {
    const Entity resolved = resolver_ != nullptr ? resolver_(entity.Id(), resolverContext_) : entity;
    const Entity snapshotEntity = resolved.IsValid() ? resolved : entity;
    const auto [it, inserted] = entityIndices_.try_emplace(snapshotEntity.Id(), snapshot_.entities.size());
    if (inserted) {
        snapshot_.entities.push_back(EntitySnapshot{
            .id = snapshotEntity.Id(),
            .name = ReadName(world, snapshotEntity),
            .components = {},
        });
    }
    return snapshot_.entities[it->second];
}

std::string SnapshotEntityIndex::ReadName(ecs_world_t* world, Entity entity) {
    const char* name = ecs_get_name(world, FlecsEntityId(entity));
    return name == nullptr ? std::string{} : std::string{ name };
}

} // namespace kb::ecs
