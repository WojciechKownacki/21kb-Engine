#pragma once

#include "engine/ecs/ComponentTypeInfo.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/WorldInspection.hpp"

#include <span>

struct ecs_world_t;

namespace kb::ecs {

class EntityInspector {
public:
    [[nodiscard]] static EntityInspection Inspect(ecs_world_t* world, Entity entity, Entity parent, std::span<const ComponentTypeInfo> componentTypes);
};

} // namespace kb::ecs
