#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

namespace kb::scene {

class Scene;

class SceneHierarchyParentService {
public:
    SceneHierarchyParentService() = delete;

    [[nodiscard]] static SceneObject Parent(Scene& scene, SceneObject object);
    [[nodiscard]] static SceneEntity Parent(const Scene& scene, SceneEntity entity) noexcept;
};

} // namespace kb::scene
