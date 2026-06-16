#include "ecs/inspection/EntityInspector.hpp"

#include "ecs/FlecsEntityIds.hpp"
#include "ecs/inspection/EntityComponentInspectionCollector.hpp"
#include "ecs/inspection/EntityNameReader.hpp"

#include <flecs.h>

namespace kb::ecs {

EntityInspection EntityInspector::Inspect(ecs_world_t* world, Entity entity, Entity parent, std::span<const ComponentTypeInfo> componentTypes) {
    EntityInspection inspection;
    if (world == nullptr || !entity.IsValid() || !ecs_is_alive(world, FlecsEntityId(entity))) {
        return inspection;
    }

    inspection.entity = entity;
    inspection.name = EntityNameReader::Read(world, entity);
    inspection.parent = parent;
    inspection.components = EntityComponentInspectionCollector::Collect(world, entity, componentTypes);

    return inspection;
}

} // namespace kb::ecs
