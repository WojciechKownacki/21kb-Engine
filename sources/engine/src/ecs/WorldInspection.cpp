#include "engine/ecs/World.hpp"

#include "ecs/ComponentRegistry.hpp"
#include "ecs/inspection/EntityInspector.hpp"

namespace kb::ecs {

EntityInspection World::InspectEntity(Entity entity) const {
    if (components_ == nullptr) {
        return {};
    }
    return EntityInspector::Inspect(world_, entity, Parent(entity), components_->Types());
}

} // namespace kb::ecs
