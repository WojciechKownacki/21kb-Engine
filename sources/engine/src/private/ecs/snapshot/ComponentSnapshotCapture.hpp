#pragma once

#include "engine/ecs/ComponentTypeInfo.hpp"

struct ecs_world_t;

namespace kb::ecs {

class SnapshotEntityIndex;

class ComponentSnapshotCapture {
public:
    static void Capture(ecs_world_t* world, const ComponentTypeInfo& componentType, SnapshotEntityIndex& entities);
};

} // namespace kb::ecs
