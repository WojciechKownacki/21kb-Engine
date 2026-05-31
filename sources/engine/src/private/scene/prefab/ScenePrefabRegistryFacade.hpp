#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace kb::scene {

class Scene;

class ScenePrefabRegistryFacade {
public:
    ScenePrefabRegistryFacade() = delete;

    [[nodiscard]] static ScenePrefabHandle Register(Scene& scene, std::string name, ScenePrefab prefab);
    [[nodiscard]] static ScenePrefabHandle RegisterVariant(Scene& scene, std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides);
    [[nodiscard]] static bool Contains(Scene& scene, ScenePrefabHandle handle) noexcept;
    [[nodiscard]] static std::string Guid(Scene& scene, ScenePrefabHandle handle);
    [[nodiscard]] static std::size_t Count(Scene& scene) noexcept;
    [[nodiscard]] static ScenePrefab Get(Scene& scene, ScenePrefabHandle handle);
};

} // namespace kb::scene
