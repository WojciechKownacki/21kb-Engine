#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"

#include <cstddef>

namespace kb::scene {

class Scene;
class ScenePrefabRegistry;
struct ScenePrefabInstanceRecord;

class ScenePrefabInstanceSynchronizer {
public:
    ScenePrefabInstanceSynchronizer() = delete;

    [[nodiscard]] static std::size_t Refresh(Scene& scene, ScenePrefabHandle handle);
    [[nodiscard]] static bool RefreshInstance(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance);
};

} // namespace kb::scene
