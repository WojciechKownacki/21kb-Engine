#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"

#include <vector>

namespace kb::scene {

class Scene;

class SceneHierarchyRootsService {
public:
    SceneHierarchyRootsService() = delete;

    [[nodiscard]] static std::vector<SceneObject> RootObjects(Scene& scene);
    [[nodiscard]] static std::vector<SceneEntity> RootEntities(const Scene& scene);
};

} // namespace kb::scene
