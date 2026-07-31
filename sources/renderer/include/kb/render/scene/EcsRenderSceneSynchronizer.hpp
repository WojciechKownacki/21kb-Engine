#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::ecs {
class WorkerPool;
}

namespace kb::render {

class RenderScene;

struct EcsRenderSceneSynchronizerReserveDesc {
    std::uint32_t meshProxies = 0;
    std::uint32_t cameraProxies = 0;
    std::uint32_t lightProxies = 0;
    std::uint32_t visibilityBlockerProxies = 0;
    std::uint32_t transformCacheEntries = 0;
    std::uint32_t transformResolvingEntries = 0;
    std::uint32_t transformUpdateEntities = 0;
};

struct EcsRenderSceneSynchronizerStats {
    std::uint32_t meshSeenCount = 0;
    std::uint32_t cameraSeenCount = 0;
    std::uint32_t lightSeenCount = 0;
    std::uint32_t visibilityBlockerSeenCount = 0;
    std::uint32_t meshSeenCapacity = 0;
    std::uint32_t cameraSeenCapacity = 0;
    std::uint32_t lightSeenCapacity = 0;
    std::uint32_t visibilityBlockerSeenCapacity = 0;
    std::uint32_t transformCacheCount = 0;
    std::uint32_t transformResolvingCount = 0;
    std::uint32_t transformUpdateEntityCount = 0;
    std::uint32_t transformCacheCapacity = 0;
    std::uint32_t transformResolvingCapacity = 0;
    std::uint32_t transformUpdateEntityCapacity = 0;
    // H3 telemetry: how many world reads consumed the precomputed transform
    // (fast path) versus fell back to a full recursive resolve.
    std::uint32_t transformPrecomputedReadCount = 0;
    std::uint32_t transformResolvedFallbackCount = 0;
};

class EcsRenderSceneSynchronizer {
public:
    void Reserve(const EcsRenderSceneSynchronizerReserveDesc& desc);
    void Sync(const kb::scene::Scene& scene, RenderScene& renderScene) const;
    void SyncEntities(const kb::scene::Scene& scene, RenderScene& renderScene, std::span<const std::uint64_t> entityIds) const;
    void SyncTransformUpdates(const kb::scene::Scene& scene, RenderScene& renderScene) const;
    void SyncMeshRendererUpdates(const kb::scene::Scene& scene, RenderScene& renderScene) const;

    // H2 - columnar transform-only sync. Consumes the world affine matrices the
    // batched transform system already produced for changed entities and pushes
    // them straight into the render instance stream (no per-entity component
    // lookup, no proxy desc compare). Entities without a mesh proxy are skipped;
    // structural changes still go through Sync/SyncEntities.
    void SyncMeshWorldAffines(
        RenderScene& renderScene,
        std::span<const kb::scene::SceneEntity> entities,
        std::span<const kb::scene::WorldTransformAffine3x4> worldAffines) const;

    // H6 - the same columnar transform-only sync, parallelized over the shared
    // WorkerPool. Each entity owns a distinct proxy and instance slot, so the
    // in-place refreshes run concurrently; invalidation/telemetry are applied
    // once after the batch.
    void SyncMeshWorldAffinesParallel(
        RenderScene& renderScene,
        std::span<const kb::scene::SceneEntity> entities,
        std::span<const kb::scene::WorldTransformAffine3x4> worldAffines,
        kb::ecs::WorkerPool& workerPool,
        std::size_t grainSize = 4096U) const;
    [[nodiscard]] EcsRenderSceneSynchronizerStats Stats() const noexcept;

private:
    mutable std::vector<std::uint64_t> seenMeshes_;
    mutable std::vector<std::uint64_t> seenCameras_;
    mutable std::vector<std::uint64_t> seenLights_;
    mutable std::vector<std::uint64_t> seenVisibilityBlockers_;
    mutable std::vector<std::uint64_t> transformUpdateEntities_;
    mutable std::unordered_map<std::uint64_t, kb::scene::TransformComponent> transformCache_;
    mutable std::unordered_set<std::uint64_t> transformResolving_;
    mutable std::size_t transformPrecomputedReadCount_ = 0;
    mutable std::size_t transformResolvedFallbackCount_ = 0;
};

using RenderSyncSystem = EcsRenderSceneSynchronizer;

} // namespace kb::render
