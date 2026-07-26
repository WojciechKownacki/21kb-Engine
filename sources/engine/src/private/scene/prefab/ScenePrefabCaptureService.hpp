#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "engine/scene/SceneObject.hpp"

#include <span>

namespace kb::scene {

class Scene;

class ScenePrefabCaptureService {
public:
    [[nodiscard]] static ScenePrefab Capture(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings);
    [[nodiscard]] static ScenePrefab CaptureRoots(Scene& scene, std::span<const SceneObject> roots, const ScenePrefabCaptureSettings& settings);
};

} // namespace kb::scene
