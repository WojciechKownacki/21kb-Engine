#pragma once

#include "engine/ecs/ComponentTypeInfo.hpp"
#include "engine/ecs/WorldSnapshot.hpp"
#include "ecs/snapshot/SnapshotEntityIndex.hpp"

#include <span>

struct ecs_world_t;

namespace kb::ecs {

class WorldSnapshotBuilder {
public:
    [[nodiscard]] static WorldSnapshot Capture(
        ecs_world_t* world,
        std::span<const ComponentTypeInfo> componentTypes,
        SnapshotEntityIndex::EntityResolver resolver = nullptr,
        void* resolverContext = nullptr);
};

} // namespace kb::ecs
