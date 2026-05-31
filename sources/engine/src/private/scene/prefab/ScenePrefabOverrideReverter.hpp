#pragma once

#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

namespace kb::scene {

class Scene;
class ScenePrefab;

class ScenePrefabOverrideReverter {
public:
    ScenePrefabOverrideReverter() = delete;

    [[nodiscard]] static bool Revert(Scene& scene, const ScenePrefab& prefab, ScenePrefabInstanceRecord& instance);
};

} // namespace kb::scene
