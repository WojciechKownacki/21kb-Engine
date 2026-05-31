#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "scene/prefab/ScenePrefabNestedNodeMapping.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class ScenePrefabNestedNodeMapper {
public:
    ScenePrefabNestedNodeMapper() = delete;

    [[nodiscard]] static ScenePrefabNestedNodeMapping Append(ScenePrefab& output, const ScenePrefab& source, const ScenePrefab& nestedPrefab, const ScenePrefabNodeDesc& overlayRoot, const std::vector<std::uint32_t>& sourceSubtree, std::uint32_t outputParent);
};

} // namespace kb::scene
