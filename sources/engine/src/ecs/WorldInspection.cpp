#include "engine/ecs/World.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/inspection/EditorWorldInspector.hpp"
#include "ecs/inspection/EntityInspector.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

namespace kb::ecs {

EntityInspection World::InspectEntity(Entity entity) const {
    if (registries_ == nullptr) {
        return {};
    }
    return EntityInspector::Inspect(world_, entity, Parent(entity), registries_->Components().Types());
}

bool World::CaptureEditorWorld(EditorWorldInspection& output) const {
    if (registries_ == nullptr) {
        output.entities.clear();
        return false;
    }
    return EditorWorldInspector::Inspect(*this, output);
}

} // namespace kb::ecs
