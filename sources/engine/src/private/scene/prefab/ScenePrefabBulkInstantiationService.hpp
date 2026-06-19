#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstddef>
#include <vector>

namespace kb::scene {

class Scene;
class ScenePrefabBakedData;
struct ScenePrefabInstantiationStats;

class ScenePrefabBulkInstantiationService {
public:
    ScenePrefabBulkInstantiationService() = delete;

    [[nodiscard]] static std::vector<ScenePrefabInstance> Instantiate(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static std::vector<ScenePrefabInstance> InstantiateBaked(
        Scene& scene,
        const ScenePrefab& prefab,
        const ScenePrefabBakedData& baked,
        std::size_t count,
        const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static ScenePrefabInstantiationStats InstantiateBatch(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static ScenePrefabInstantiationStats InstantiateBatchBaked(
        Scene& scene,
        const ScenePrefab& prefab,
        const ScenePrefabBakedData& baked,
        std::size_t count,
        const ScenePrefabInstantiationSettings& settings);
};

} // namespace kb::scene
