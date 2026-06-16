#include "scene/entities/SceneEntityNaming.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

#include <cstddef>
#include <stdexcept>

namespace kb::scene {

std::string SceneEntityNaming::Name(const kb::ecs::World& world, SceneEntity entity) {
    return world.Name(entity);
}

void SceneEntityNaming::SetName(kb::ecs::World& world, SceneEntity entity, std::string_view name) {
    const std::string ownedName{ name };
    ecs_set_name(world.NativeHandle(), kb::ecs::FlecsEntityId(entity), ownedName.c_str());
}

void SceneEntityNaming::SetNames(kb::ecs::World& world, std::span<const SceneEntity> entities, std::span<const std::string> names) {
    if (entities.size() != names.size()) {
        throw std::invalid_argument("Scene entity bulk naming requires matching entity and name counts");
    }
    for (std::size_t index = 0; index < entities.size(); ++index) {
        const std::string& name = names[index];
        if (!name.empty()) {
            ecs_set_name(world.NativeHandle(), kb::ecs::FlecsEntityId(entities[index]), name.c_str());
        }
    }
}

} // namespace kb::scene
