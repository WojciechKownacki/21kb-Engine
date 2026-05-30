#pragma once

#include "engine/ecs/ComponentTypeInfo.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/WorldInspection.hpp"

#include <span>
#include <vector>

struct ecs_world_t;

namespace kb::ecs {

class EntityComponentInspectionCollector {
public:
    [[nodiscard]] static std::vector<EntityComponentInspection> Collect(
        ecs_world_t* world,
        Entity entity,
        std::span<const ComponentTypeInfo> componentTypes);

private:
    [[nodiscard]] static EntityComponentInspection ToInspection(const ComponentTypeInfo& componentType);
};

} // namespace kb::ecs
