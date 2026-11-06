#pragma once

#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstdint>
#include <span>

namespace kb::scene {

class ScenePrefabComponentOverrideReporter {
public:
    ScenePrefabComponentOverrideReporter() = delete;

    static void Append(SceneComponents components, SceneEntity entity, const ScenePrefabNodeComponents& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object);
    // The exposed script variables of a live behaviour against the ones its prefab node carries: one
    // "behaviour.variable.<name>" property per variable that differs, valued as typed text, or "default" for
    // a variable the prefab overrides and the instance does not.
    static void AppendBehaviourVariables(std::span<const BehaviourVariableOverride> actual, const ScenePrefabNodeComponents& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object);
};

} // namespace kb::scene
