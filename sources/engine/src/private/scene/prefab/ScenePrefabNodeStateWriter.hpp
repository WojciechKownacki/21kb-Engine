#pragma once

#include "engine/scene/ScenePrefabNode.hpp"

namespace kb::scene {

class Scene;
class SceneObject;

class ScenePrefabNodeStateWriter {
public:
    ScenePrefabNodeStateWriter() = delete;

    static void Write(Scene& scene, SceneObject object, SceneObject parent, const ScenePrefabNodeDesc& node);
};

} // namespace kb::scene
