#pragma once

#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabInstantiationSettings.hpp"
#include "engine/scene/ScenePrefabNode.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace kb::scene {

class ScenePrefab {
public:
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t NodeCount() const noexcept;
    [[nodiscard]] std::span<const ScenePrefabNodeDesc> Nodes() const noexcept;
    [[nodiscard]] const ScenePrefabNodeDesc* TryGetNode(std::uint32_t nodeIndex) const noexcept;
    [[nodiscard]] ScenePrefabNodeDesc* TryGetMutableNode(std::uint32_t nodeIndex) noexcept;
    [[nodiscard]] std::uint32_t FindNodeIndexByStableId(std::uint64_t stableId) const noexcept;
    [[nodiscard]] const ScenePrefabNodeDesc* TryGetNodeByStableId(std::uint64_t stableId) const noexcept;
    [[nodiscard]] ScenePrefabNodeDesc* TryGetMutableNodeByStableId(std::uint64_t stableId) noexcept;
    [[nodiscard]] std::uint32_t ResolveNodeIndex(const ScenePrefabPropertyOverride& property) const noexcept;

    [[nodiscard]] std::uint32_t AddNode(ScenePrefabNodeDesc desc);
    void Reserve(std::size_t nodeCount);
    // For readers: a node count read from a file is only a claim until the nodes
    // themselves have been read, and each node takes kilobytes. Room is made for a
    // bounded share of it; the rest grows as nodes actually arrive.
    void ReserveDeclared(std::size_t declaredNodeCount);
    void Clear() noexcept;

private:
    std::vector<ScenePrefabNodeDesc> nodes_;
    std::uint64_t nextStableId_ = 1U;
    // Retained mutable pointers can change ids between subsequent appends.
    bool mutableNodesExposed_ = false;
};

} // namespace kb::scene
