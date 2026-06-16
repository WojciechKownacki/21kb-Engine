#include "ecs/snapshot/WorldSnapshotBuilder.hpp"

#include "ecs/snapshot/ComponentSnapshotCapture.hpp"
#include "ecs/snapshot/SnapshotEntityIndex.hpp"

#include <algorithm>

namespace kb::ecs {

namespace {

void SortEntitiesById(WorldSnapshot& snapshot) {
    std::sort(snapshot.entities.begin(), snapshot.entities.end(), [](const EntitySnapshot& lhs, const EntitySnapshot& rhs) {
        return lhs.id < rhs.id;
    });
}

} // namespace

WorldSnapshot WorldSnapshotBuilder::Capture(
    ecs_world_t* world,
    std::span<const ComponentTypeInfo> componentTypes,
    SnapshotEntityIndex::EntityResolver resolver,
    void* resolverContext) {
    WorldSnapshot snapshot;
    if (world == nullptr) {
        return snapshot;
    }

    snapshot.componentTypes.assign(componentTypes.begin(), componentTypes.end());
    SnapshotEntityIndex entityIndex{ snapshot, resolver, resolverContext };
    for (const ComponentTypeInfo& componentType : componentTypes) {
        ComponentSnapshotCapture::Capture(world, componentType, entityIndex);
    }

    SortEntitiesById(snapshot);

    return snapshot;
}

} // namespace kb::ecs
