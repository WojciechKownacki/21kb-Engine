#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstddef>
#include <vector>

namespace kb::scene {

class Scene;
struct ScenePrefabInstantiationStats;

class ScenePrefabInstantiationService {
public:
    ScenePrefabInstantiationService() = delete;

    [[nodiscard]] static ScenePrefabInstance Instantiate(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static std::vector<ScenePrefabInstance> InstantiateMany(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static ScenePrefabInstantiationStats InstantiateBatch(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings);
};

} // namespace kb::scene
