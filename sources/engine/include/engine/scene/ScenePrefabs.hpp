#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabCaptureSettings.hpp"

namespace kb::scene {

class Scene;
class SceneObject;

class ScenePrefabs {
public:
    explicit ScenePrefabs(Scene& scene) noexcept;

    [[nodiscard]] ScenePrefab Capture(SceneObject root) const;
    [[nodiscard]] ScenePrefab Capture(SceneObject root, const ScenePrefabCaptureSettings& settings) const;
    [[nodiscard]] ScenePrefabInstance Instantiate(const ScenePrefab& prefab);
    [[nodiscard]] ScenePrefabInstance Instantiate(const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings);

private:
    Scene& scene_;
};

} // namespace kb::scene
