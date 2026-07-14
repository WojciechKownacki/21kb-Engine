#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

#include <cstddef>
#include <vector>

namespace kb::scene {

class Scene;

class SceneHierarchyChildrenService {
public:
    SceneHierarchyChildrenService() = delete;

    [[nodiscard]] static std::vector<SceneObject> Children(Scene& scene, SceneObject object);
    [[nodiscard]] static std::vector<SceneEntity> ChildEntities(const Scene& scene, SceneEntity entity);
    [[nodiscard]] static std::size_t ChildCount(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SceneEntity ChildAt(const Scene& scene, SceneEntity entity, std::size_t index) noexcept;
};

} // namespace kb::scene
