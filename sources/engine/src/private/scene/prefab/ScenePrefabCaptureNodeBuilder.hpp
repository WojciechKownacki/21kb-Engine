#pragma once

#include "engine/scene/ScenePrefabNode.hpp"

#include <cstdint>

namespace kb::scene {

class Scene;
class SceneObject;

class ScenePrefabCaptureNodeBuilder {
public:
    [[nodiscard]] static ScenePrefabNodeDesc Build(Scene& scene, SceneObject object, std::uint32_t parentNode);
};

} // namespace kb::scene
