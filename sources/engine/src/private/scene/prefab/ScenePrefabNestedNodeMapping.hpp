#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace kb::scene {

constexpr std::uint32_t kScenePrefabUnmappedNode = std::numeric_limits<std::uint32_t>::max();

struct ScenePrefabNestedNodeMapping {
    std::vector<std::uint32_t> sourceSubtree;
    std::vector<std::uint32_t> sourceToOutput;
    std::size_t nestedNodeCount = 0;
};

} // namespace kb::scene
