#include "ecs/inspection/EditorWorldInspector.hpp"

#include "ecs/inspection/EditorEntityInspectionBuilder.hpp"
#include "ecs/ComponentRegistry.hpp"
#include "ecs/world/WorldInternalAccess.hpp"
#include "ecs/world/WorldEntityCatalog.hpp"
#include "ecs/world/WorldRegistrySet.hpp"
#include "engine/ecs/World.hpp"

namespace kb::ecs {

bool EditorWorldInspector::Inspect(const World& world, EditorWorldInspection& output) {
    output.entities.clear();

    const WorldRegistrySet* registries = WorldInternalAccess::Registries(world);
    if (registries == nullptr) {
        return false;
    }

    const std::vector<Entity> entities = registries->Entities().AliveEntities(WorldInternalAccess::Native(world));
    const std::span<const ComponentTypeInfo> componentTypes = registries->Components().Types();
    output.entities.reserve(entities.size());
    for (Entity entity : entities) {
        EditorEntityInspection& entityInspection = output.entities.emplace_back();
        try {
            if (EditorEntityInspectionBuilder::Build(world, entity, componentTypes, entityInspection)) {
                continue;
            }
        } catch (...) {
            output.entities.pop_back();
            throw;
        }
        output.entities.pop_back();
        return false;
    }

    return true;
}

} // namespace kb::ecs
