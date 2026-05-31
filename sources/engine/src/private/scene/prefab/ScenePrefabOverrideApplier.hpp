#pragma once

#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

namespace kb::scene {

class Scene;
class ScenePrefab;

class ScenePrefabOverrideApplier {
public:
    ScenePrefabOverrideApplier() = delete;

    [[nodiscard]] static bool Apply(Scene& scene, ScenePrefab& prefab, ScenePrefabInstanceRecord& instance);
};

} // namespace kb::scene
