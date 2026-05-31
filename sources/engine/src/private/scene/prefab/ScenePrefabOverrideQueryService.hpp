#pragma once

#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

namespace kb::scene {

class Scene;

class ScenePrefabOverrideQueryService {
public:
    ScenePrefabOverrideQueryService() = delete;

    [[nodiscard]] static bool IsInstance(Scene& scene, ScenePrefabInstanceHandle handle) noexcept;
    [[nodiscard]] static ScenePrefabOverrideReport Overrides(Scene& scene, ScenePrefabInstanceHandle handle);
};

} // namespace kb::scene
