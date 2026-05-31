#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

namespace kb::scene {

class SceneComponents;

class ScenePrefabComponentComparator {
public:
    ScenePrefabComponentComparator() = delete;

    [[nodiscard]] static ScenePrefabOverrideFlag Compare(SceneComponents components, SceneEntity entity, const ScenePrefabNodeComponents& expected) noexcept;
};

} // namespace kb::scene
