#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstddef>
#include <span>

namespace kb::scene {

class Scene;
class ScenePrefabRegistry;
struct ScenePrefabInstanceRecord;

class ScenePrefabInstanceSynchronizer {
public:
    ScenePrefabInstanceSynchronizer() = delete;

    [[nodiscard]] static std::size_t Refresh(Scene& scene, ScenePrefabHandle handle);
    [[nodiscard]] static bool RefreshInstance(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance);
    // Brings objects captured against an older version of the instance's prefab to its current
    // content, keeping only the given overrides: nodes the prefab gained are created, every other
    // property takes the prefab's value.
    [[nodiscard]] static bool Rebase(Scene& scene, ScenePrefabInstanceRecord& instance, std::span<const ScenePrefabPropertyOverride> overrides);
};

} // namespace kb::scene
