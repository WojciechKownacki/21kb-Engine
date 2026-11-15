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

    // The instance at `position` changed. Different threads may update different instances of one group at once; the bounds
    // that depend on them are left stale until Refresh.
    static void Update(SceneMeshVisibilityIndex& index, const SceneRenderMeshInstance& instance, std::uint32_t position) noexcept;

    // Brings the stale bounds up to date. Instances that drifted far apart since the clusters were made are indexed again.
    static void Refresh(std::span<const SceneRenderMeshInstance> instances, SceneMeshVisibilityIndex& index);
};

} // namespace kb::render
