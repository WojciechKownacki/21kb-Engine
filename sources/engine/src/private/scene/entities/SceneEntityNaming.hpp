#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <span>
#include <string>
#include <string_view>

namespace kb::scene {

class SceneState;

class SceneEntityNaming {
public:
    SceneEntityNaming() = delete;

    [[nodiscard]] static std::string Name(const SceneState& state, SceneEntity entity);
    static void SetName(SceneState& state, SceneEntity entity, std::string_view name);
    static void SetNames(SceneState& state, std::span<const SceneEntity> entities, std::span<const std::string> names);
    static void SetRepeatedNames(SceneState& state, std::span<const SceneEntity> entities, std::span<const std::string> namesByNode);
    static void ClearName(SceneState& state, SceneEntity entity) noexcept;
};

} // namespace kb::scene
