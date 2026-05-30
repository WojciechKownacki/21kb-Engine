#include "ecs/inspection/EditorWorldInspector.hpp"

#include "ecs/inspection/EditorEntityInspectionBuilder.hpp"
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
    output.entities.reserve(entities.size());
    for (Entity entity : entities) {
        EditorEntityInspection entityInspection;
        if (!EditorEntityInspectionBuilder::Build(world, entity, entityInspection)) {
            return false;
        }
        output.entities.push_back(std::move(entityInspection));
    }

    return true;
}

} // namespace kb::ecs
