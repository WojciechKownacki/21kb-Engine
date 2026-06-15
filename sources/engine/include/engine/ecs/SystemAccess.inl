#pragma once

#include "engine/ecs/World.hpp"

namespace kb::ecs {

template <typename T>
SystemAccess& SystemAccess::Read(World& world) {
    return Read(world.RegisterComponent<T>());
}

template <typename T>
SystemAccess& SystemAccess::Write(World& world) {
    return Write(world.RegisterComponent<T>());
}

} // namespace kb::ecs
