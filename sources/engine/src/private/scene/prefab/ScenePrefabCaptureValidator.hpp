#pragma once

#include "engine/scene/SceneObject.hpp"

namespace kb::scene {

class Scene;

class ScenePrefabCaptureValidator {
public:
    [[nodiscard]] static bool CanCapture(const Scene& scene, SceneObject root) noexcept;
};

} // namespace kb::scene
