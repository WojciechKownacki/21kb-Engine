#include "ecs/inspection/EntityComponentInspectionCollector.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

namespace kb::ecs {

std::vector<EntityComponentInspection> EntityComponentInspectionCollector::Collect(
    ecs_world_t* world,
    Entity entity,
    std::span<const ComponentTypeInfo> componentTypes) {
    std::vector<EntityComponentInspection> components;
    for (const ComponentTypeInfo& componentType : componentTypes) {
        if (componentType.id != 0 && ecs_has_id(world, FlecsEntityId(entity), componentType.id)) {
            components.push_back(ToInspection(componentType));
        }
    }
    return components;
}

EntityComponentInspection EntityComponentInspectionCollector::ToInspection(const ComponentTypeInfo& componentType) {
    return EntityComponentInspection{
        .id = componentType.id,
        .name = componentType.name,
        .size = componentType.size,
        .alignment = componentType.alignment,
    };
}

} // namespace kb::ecs
