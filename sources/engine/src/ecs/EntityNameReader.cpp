#include "ecs/inspection/EntityNameReader.hpp"

#include "ecs/FlecsEntityIds.hpp"

#include <flecs.h>

namespace kb::ecs {

std::string EntityNameReader::Read(ecs_world_t* world, Entity entity) {
    const char* name = ecs_get_name(world, FlecsEntityId(entity));
    return name == nullptr ? std::string{} : std::string{ name };
}

} // namespace kb::ecs
