#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstdint>
#include <string>

namespace kb::scene {

class Scene;

class ScenePrefabOverridePropertyReporter {
public:
    ScenePrefabOverridePropertyReporter() = delete;

    static void Add(ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject target, std::string propertyPath, std::string value, ScenePrefabOverrideFlag flag, SceneObject objectReference = {});
    static void AppendChangedProperties(Scene& scene, const ScenePrefabNodeDesc& node, SceneObject expectedParent, std::uint32_t nodeIndex, SceneObject object, ScenePrefabOverrideReport& report);
};

} // namespace kb::scene
