#include "ecs/inspection/EditorEntityInspectionBuilder.hpp"

#include "ecs/FlecsEntityIds.hpp"
#include "ecs/world/WorldInternalAccess.hpp"
#include "engine/ecs/World.hpp"

#include <flecs.h>

namespace kb::ecs {

bool EditorEntityInspectionBuilder::Build(
    const World& world,
    Entity entity,
    std::span<const ComponentTypeInfo> componentTypes,
    EditorEntityInspection& output) {
    const ecs_world_t* native = WorldInternalAccess::Native(world);
    if (native == nullptr || !entity.IsValid() || !ecs_is_alive(native, FlecsEntityId(entity))) {
        return false;
    }

    output.entity = entity;
    const char* name = ecs_get_name(native, FlecsEntityId(entity));
    output.name = name == nullptr ? std::string{} : std::string{ name };
    output.parent = world.Parent(entity);
    output.components.clear();

    for (const ComponentTypeInfo& component : componentTypes) {
        if (component.id == 0 || !ecs_has_id(native, FlecsEntityId(entity), component.id)) {
            continue;
        }
        SerializedComponent serializedComponent;
        if (!world.SerializeComponent(entity, component.id, serializedComponent)) {
            return false;
        }
        output.components.push_back(std::move(serializedComponent));
    }

    return true;
}

} // namespace kb::ecs
