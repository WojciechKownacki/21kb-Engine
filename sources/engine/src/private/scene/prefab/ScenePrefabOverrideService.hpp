#pragma once

#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

namespace kb::scene {

class Scene;

class ScenePrefabOverrideService {
public:
    ScenePrefabOverrideService() = delete;

    [[nodiscard]] static bool IsInstance(Scene& scene, ScenePrefabInstanceHandle handle) noexcept;
    [[nodiscard]] static ScenePrefabOverrideReport Overrides(Scene& scene, ScenePrefabInstanceHandle handle);
    [[nodiscard]] static bool Revert(Scene& scene, ScenePrefabInstanceHandle handle);
    [[nodiscard]] static bool Apply(Scene& scene, ScenePrefabInstanceHandle handle);
};

} // namespace kb::scene
