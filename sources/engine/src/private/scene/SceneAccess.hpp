#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

namespace kb::scene {

class SceneAccess {
public:
    SceneAccess() = delete;

    [[nodiscard]] static SceneState& State(Scene& scene) noexcept;
    [[nodiscard]] static const SceneState& State(const Scene& scene) noexcept;
    [[nodiscard]] static SceneObject MakeObject(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool BelongsTo(Scene& scene, SceneObject object) noexcept;
    [[nodiscard]] static bool BelongsTo(const Scene& scene, SceneObject object) noexcept;
};

} // namespace kb::scene
