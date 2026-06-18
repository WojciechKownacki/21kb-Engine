#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabInstantiationSettings.hpp"

#include <cstddef>
#include <vector>

namespace kb::scene {

class Scene;
struct ScenePrefabInstantiationStats;

class ScenePrefabInstanceFacade {
public:
    ScenePrefabInstanceFacade() = delete;

    [[nodiscard]] static ScenePrefabInstance Instantiate(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static ScenePrefabInstance Instantiate(Scene& scene, ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static std::vector<ScenePrefabInstance> InstantiateMany(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static std::vector<ScenePrefabInstance> InstantiateMany(Scene& scene, ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static ScenePrefabInstantiationStats InstantiateBatch(Scene& scene, const ScenePrefab& prefab, std::size_t count, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static ScenePrefabInstantiationStats InstantiateBatch(Scene& scene, ScenePrefabHandle handle, std::size_t count, const ScenePrefabInstantiationSettings& settings);
};

} // namespace kb::scene
