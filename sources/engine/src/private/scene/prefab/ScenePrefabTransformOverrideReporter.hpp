#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstdint>

namespace kb::scene {

class Scene;

class ScenePrefabTransformOverrideReporter {
public:
    ScenePrefabTransformOverrideReporter() = delete;

    static void Append(Scene& scene, const ScenePrefabNodeDesc& node, std::uint32_t nodeIndex, SceneObject object, ScenePrefabOverrideReport& report);
};

} // namespace kb::scene
