#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstdint>

namespace kb::scene {

class Scene;

class ScenePrefabObjectOverrideReporter {
public:
    ScenePrefabObjectOverrideReporter() = delete;

    [[nodiscard]] static bool Append(Scene& scene, const ScenePrefabNodeDesc& node, SceneObject expectedParent, std::uint32_t nodeIndex, SceneObject object, ScenePrefabOverrideReport& report);
};

} // namespace kb::scene
