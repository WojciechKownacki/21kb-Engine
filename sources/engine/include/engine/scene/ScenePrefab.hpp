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

    [[nodiscard]] std::uint32_t AddNode(ScenePrefabNodeDesc desc);
    void Reserve(std::size_t nodeCount);
    void Clear() noexcept;

private:
    std::vector<ScenePrefabNodeDesc> nodes_;
};

} // namespace kb::scene
