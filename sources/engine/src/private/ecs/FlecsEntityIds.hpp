#pragma once

#include "engine/ecs/Entity.hpp"

#include <flecs.h>

namespace kb::ecs {

[[nodiscard]] inline ecs_entity_t FlecsEntityId(Entity entity) noexcept {
    return ecs_strip_generation(entity.Id());
}

} // namespace kb::ecs
