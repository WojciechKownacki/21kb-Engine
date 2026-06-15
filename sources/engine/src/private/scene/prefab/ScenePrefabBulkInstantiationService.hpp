#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstddef>
#include <vector>

namespace kb::scene {

class Scene;

class ScenePrefabBulkInstantiationService {
public:
    ScenePrefabBulkInstantiationService() = delete;

    [[nodiscard]] static std::vector<ScenePrefabInstance> Instantiate(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings);
};

} // namespace kb::scene
