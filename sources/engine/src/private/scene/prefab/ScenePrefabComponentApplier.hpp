#pragma once

#include "engine/scene/ScenePrefab.hpp"

namespace kb::scene {

class Scene;

class ScenePrefabComponentApplier {
public:
    ScenePrefabComponentApplier() = delete;

    static void Apply(Scene& scene, SceneObject object, const ScenePrefabNodeComponents& components);
};

} // namespace kb::scene
