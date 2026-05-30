#pragma once

#include "engine/scene/SceneVisitors.hpp"

namespace kb::scene {

class Scene;
class SceneSystemTransformAccess;

class SceneSystemMutableTransformIteration {
public:
    SceneSystemMutableTransformIteration() = delete;

    static void ForEach(Scene& scene, SceneSystemTransformAccess& access, MutableTransformVisitor visitor, void* context);
};

} // namespace kb::scene
