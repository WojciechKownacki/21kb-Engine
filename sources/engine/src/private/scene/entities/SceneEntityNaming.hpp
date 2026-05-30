#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <string>
#include <string_view>

namespace kb::scene {

class SceneEntityNaming {
public:
    SceneEntityNaming() = delete;

    [[nodiscard]] static std::string Name(const kb::ecs::World& world, SceneEntity entity);
    static void SetName(kb::ecs::World& world, SceneEntity entity, std::string_view name);
};

} // namespace kb::scene
