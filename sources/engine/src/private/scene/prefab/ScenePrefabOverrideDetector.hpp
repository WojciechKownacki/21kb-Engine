#pragma once

#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

namespace kb::scene {

class Scene;
class ScenePrefab;

class ScenePrefabOverrideDetector {
public:
    ScenePrefabOverrideDetector() = delete;

    [[nodiscard]] static ScenePrefabOverrideReport Detect(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstanceRecord& instance);
};

} // namespace kb::scene
