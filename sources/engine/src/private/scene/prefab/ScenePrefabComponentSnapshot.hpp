#pragma once

#include "engine/scene/ScenePrefabNode.hpp"

namespace kb::scene {

class Scene;
class SceneObject;

class ScenePrefabComponentSnapshot {
public:
    [[nodiscard]] static ScenePrefabNodeComponents Capture(Scene& scene, SceneObject object);
};

} // namespace kb::scene
