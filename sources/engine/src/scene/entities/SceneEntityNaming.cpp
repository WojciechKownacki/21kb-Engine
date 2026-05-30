#include "scene/entities/SceneEntityNaming.hpp"

#include <flecs.h>

namespace kb::scene {

std::string SceneEntityNaming::Name(const kb::ecs::World& world, SceneEntity entity) {
    return world.Name(entity);
}

void SceneEntityNaming::SetName(kb::ecs::World& world, SceneEntity entity, std::string_view name) {
    const std::string ownedName{ name };
    ecs_set_name(world.NativeHandle(), entity.Id(), ownedName.c_str());
}

} // namespace kb::scene
