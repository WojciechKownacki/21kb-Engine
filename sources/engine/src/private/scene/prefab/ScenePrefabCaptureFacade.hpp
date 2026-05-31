#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/SceneObject.hpp"

#include <string>

namespace kb::scene {

class Scene;

class ScenePrefabCaptureFacade {
public:
    ScenePrefabCaptureFacade() = delete;

    [[nodiscard]] static ScenePrefab Capture(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings);
    [[nodiscard]] static ScenePrefabHandle CaptureRegistered(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name);
};

} // namespace kb::scene
