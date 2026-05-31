#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>
#include <vector>

namespace kb::scene {

class ScenePrefabNodeTreeIndex {
public:
    explicit ScenePrefabNodeTreeIndex(const ScenePrefab& prefab);

    [[nodiscard]] const std::vector<std::uint32_t>& Children(std::uint32_t nodeIndex) const;
    [[nodiscard]] std::vector<std::uint32_t> CollectPreorder(std::uint32_t rootNodeIndex) const;

private:
    void CollectPreorder(std::uint32_t nodeIndex, std::vector<std::uint32_t>& output) const;

    std::vector<std::vector<std::uint32_t>> children_;
};

} // namespace kb::scene
