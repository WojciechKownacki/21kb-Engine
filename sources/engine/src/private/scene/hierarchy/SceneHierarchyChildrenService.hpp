#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

#include <vector>

namespace kb::scene {

class Scene;

class SceneHierarchyChildrenService {
public:
    SceneHierarchyChildrenService() = delete;

    [[nodiscard]] static std::vector<SceneObject> Children(Scene& scene, SceneObject object);
    [[nodiscard]] static std::vector<SceneEntity> ChildEntities(const Scene& scene, SceneEntity entity);
};

} // namespace kb::scene
