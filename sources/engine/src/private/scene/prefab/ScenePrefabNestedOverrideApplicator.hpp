#pragma once

#include "engine/scene/ScenePrefabNode.hpp"

#include <cstdint>

namespace kb::scene {

class ScenePrefabNestedOverrideApplicator {
public:
    ScenePrefabNestedOverrideApplicator() = delete;

    static void Apply(ScenePrefabNodeDesc& node, std::uint32_t nestedNodeIndex, const ScenePrefabNodeDesc& overlayRoot);
};

} // namespace kb::scene
