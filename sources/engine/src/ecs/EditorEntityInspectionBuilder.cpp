#include "ecs/inspection/EditorEntityInspectionBuilder.hpp"

#include "engine/ecs/World.hpp"

namespace kb::ecs {

bool EditorEntityInspectionBuilder::Build(const World& world, Entity entity, EditorEntityInspection& output) {
    const EntityInspection inspection = world.InspectEntity(entity);
    if (!inspection.entity.IsValid()) {
        return false;
    }

    output.entity = inspection.entity;
    output.name = inspection.name;
    output.parent = inspection.parent;
    output.components.clear();
    output.components.reserve(inspection.components.size());

    for (const EntityComponentInspection& component : inspection.components) {
        SerializedComponent serializedComponent;
        if (!world.SerializeComponent(entity, component.id, serializedComponent)) {
            return false;
        }
        output.components.push_back(std::move(serializedComponent));
    }

    return true;
}

} // namespace kb::ecs
