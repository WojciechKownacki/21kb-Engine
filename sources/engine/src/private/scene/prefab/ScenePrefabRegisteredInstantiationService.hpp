#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabInstantiationSettings.hpp"

namespace kb::scene {

class Scene;

class ScenePrefabRegisteredInstantiationService {
public:
    ScenePrefabRegisteredInstantiationService() = delete;

    [[nodiscard]] static ScenePrefabInstance Instantiate(Scene& scene, ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings);
};

} // namespace kb::scene
