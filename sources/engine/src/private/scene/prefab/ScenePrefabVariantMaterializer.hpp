#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"

#include <span>

namespace kb::scene {

class ScenePrefabVariantMaterializer {
public:
    ScenePrefabVariantMaterializer() = delete;

    [[nodiscard]] static bool Materialize(
        const ScenePrefab& basePrefab,
        std::span<const ScenePrefabPropertyOverride> overrides,
        std::span<const ScenePrefabVariantAddedSubtree> addedChildren,
        ScenePrefab& output);
};

} // namespace kb::scene
