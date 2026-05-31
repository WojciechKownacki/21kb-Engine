#pragma once

#include "engine/scene/SceneObject.hpp"

#include <span>
#include <vector>

namespace kb::scene {

class Scene;

class SceneEntityDuplicateService {
public:
    SceneEntityDuplicateService() = delete;

    [[nodiscard]] static SceneObject Duplicate(Scene& scene, SceneObject object);
    [[nodiscard]] static std::vector<SceneObject> Duplicate(Scene& scene, std::span<const SceneObject> objects);
};

} // namespace kb::scene
