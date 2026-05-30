#pragma once

#include "engine/ecs/ComponentTypeInfo.hpp"
#include "engine/ecs/WorldSnapshot.hpp"

#include <span>

struct ecs_world_t;

namespace kb::ecs {

class WorldSnapshotBuilder {
public:
    [[nodiscard]] static WorldSnapshot Capture(ecs_world_t* world, std::span<const ComponentTypeInfo> componentTypes);
};

} // namespace kb::ecs
