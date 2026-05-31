#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <span>

namespace kb::scene {

class ScenePrefabVariantMaterializer {
public:
    ScenePrefabVariantMaterializer() = delete;

    [[nodiscard]] static bool Materialize(const ScenePrefab& basePrefab, std::span<const ScenePrefabPropertyOverride> overrides, ScenePrefab& output);
};

} // namespace kb::scene
