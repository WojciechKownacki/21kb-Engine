#pragma once

#include "engine/scene/ScenePrefabOverrides.hpp"

#include <vector>

namespace kb::scene {

class ScenePrefabVariantOverrideList {
public:
    ScenePrefabVariantOverrideList() = delete;

    [[nodiscard]] static bool Upsert(std::vector<ScenePrefabPropertyOverride> source, ScenePrefabPropertyOverride property, std::vector<ScenePrefabPropertyOverride>& output);
};

} // namespace kb::scene
