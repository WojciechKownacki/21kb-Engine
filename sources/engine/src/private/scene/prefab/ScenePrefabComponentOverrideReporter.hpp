#pragma once

#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstdint>

namespace kb::scene {

class ScenePrefabComponentOverrideReporter {
public:
    ScenePrefabComponentOverrideReporter() = delete;

    static void Append(SceneComponents components, SceneEntity entity, const ScenePrefabNodeComponents& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object);
};

} // namespace kb::scene
