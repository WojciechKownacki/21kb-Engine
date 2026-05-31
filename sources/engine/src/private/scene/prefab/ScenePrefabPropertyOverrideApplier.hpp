#pragma once

#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

namespace kb::scene {

class ScenePrefabPropertyOverrideApplier {
public:
    ScenePrefabPropertyOverrideApplier() = delete;

    [[nodiscard]] static bool Apply(ScenePrefabNodeDesc& node, const ScenePrefabPropertyOverride& property);
};

} // namespace kb::scene
