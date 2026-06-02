#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::render {

class RenderScene;

struct EcsRenderSceneSynchronizerReserveDesc {
    std::uint32_t meshProxies = 0;
    std::uint32_t cameraProxies = 0;
    std::uint32_t lightProxies = 0;
    std::uint32_t transformCacheEntries = 0;
    std::uint32_t transformResolvingEntries = 0;
};

struct EcsRenderSceneSynchronizerStats {
    std::uint32_t meshSeenCount = 0;
    std::uint32_t cameraSeenCount = 0;
    std::uint32_t lightSeenCount = 0;
    std::uint32_t meshSeenCapacity = 0;
    std::uint32_t cameraSeenCapacity = 0;
    std::uint32_t lightSeenCapacity = 0;
    std::uint32_t transformCacheCount = 0;
    std::uint32_t transformResolvingCount = 0;
    std::uint32_t transformCacheCapacity = 0;
    std::uint32_t transformResolvingCapacity = 0;
};

class EcsRenderSceneSynchronizer {
public:
    void Reserve(const EcsRenderSceneSynchronizerReserveDesc& desc);
    void Sync(const kb::scene::Scene& scene, RenderScene& renderScene) const;
    [[nodiscard]] EcsRenderSceneSynchronizerStats Stats() const noexcept;

private:
    mutable std::vector<std::uint64_t> seenMeshes_;
    mutable std::vector<std::uint64_t> seenCameras_;
    mutable std::vector<std::uint64_t> seenLights_;
    mutable std::unordered_map<std::uint64_t, kb::scene::TransformComponent> transformCache_;
    mutable std::unordered_set<std::uint64_t> transformResolving_;
};

using RenderSyncSystem = EcsRenderSceneSynchronizer;

} // namespace kb::render
