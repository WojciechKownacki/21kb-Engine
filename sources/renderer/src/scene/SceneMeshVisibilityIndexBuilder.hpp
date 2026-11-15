#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace kb::render {

// Fills the visibility index of a draw group and keeps it in step with the instances of the group, which come at its end,
// leave by giving their place to the last instance, and move.
class SceneMeshVisibilityIndexBuilder {
public:
    SceneMeshVisibilityIndexBuilder() = delete;

    // A group of fewer instances is not indexed: a pass scans it faster than it tests clusters.
    static constexpr std::size_t kMinimumInstances = 32U;

    // Indexes `instances` from scratch; `index` is left empty when there are too few of them.
    static void Build(std::span<const SceneRenderMeshInstance> instances, SceneMeshVisibilityIndex& index);

    // `instances` has just gained its last instance.
    static void Append(std::span<const SceneRenderMeshInstance> instances, SceneMeshVisibilityIndex& index);

    // The instance at `position` is about to leave its group: the last instance takes its place.
    static void Remove(SceneMeshVisibilityIndex& index, std::uint32_t position) noexcept;

    // The instance at `position` moved, or changed; its entry is made again by the next Refresh, from the instance. Different
    // threads may mark different instances of one group at once.
    static void MarkMoved(SceneMeshVisibilityIndex& index, std::uint32_t position) noexcept;
    static void MarkChanged(SceneMeshVisibilityIndex& index, std::uint32_t position) noexcept;

    // Makes again the entries of the instances that moved or changed and the bounds they leave stale. Instances that drifted
    // far apart since the clusters were made are indexed again.
    static void Refresh(std::span<const SceneRenderMeshInstance> instances, SceneMeshVisibilityIndex& index);
};

} // namespace kb::render
