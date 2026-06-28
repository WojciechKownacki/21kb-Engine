#pragma once

#include "kb/render/Renderer.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/runtime/RuntimeFrameResourceReferences.hpp"
#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"
#include "kb/render/runtime/RuntimeRenderResourceCache.hpp"
#include "kb/render/scene/EcsRenderSceneSynchronizer.hpp"
#include "kb/render/scene/RenderSceneStore.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <cstdint>

namespace kb::render {

struct RendererRuntimeResourceStatsBuildDesc {
    RuntimeRenderResourceCacheStats cacheStats{};
    RuntimeFrameResourceReferenceStats referenceStats{};
    RuntimeRenderAssetDiscoveryStats discoveryStats{};
    RenderSceneStoreStats storeStats{};
    RenderResourceRegistryStats resourceStats{};
    SceneRenderResourceMapStats resourceMapStats{};
    EcsRenderSceneSynchronizerStats syncStats{};
    SceneRenderLightingConfig defaultLightingConfig{};
    std::uint32_t unresolvedMaterialTexturePathCount = 0;
    std::uint32_t defaultMaterialFallbackCount = 0;
    std::uint32_t errorMaterialFallbackCount = 0;
    std::uint32_t materialLoadedCount = 0;
    std::uint32_t materialFallbackCount = 0;
    std::uint32_t materialErrorCount = 0;
    std::uint32_t materialReloadCount = 0;
    std::uint32_t materialResolverDiagnosticCount = 0;
    std::uint32_t scenePassSubmitStatsCapacity = 0;
    std::uint32_t shadowMapSize = 0;
    std::uint64_t shadowMapAllocationBytes = 0;
    bool shadowMapAllocated = false;
    std::uint64_t retentionFrames = Renderer::kRuntimeAssetRetentionFrames;
    std::uint64_t assetDiscoveryIntervalFrames = Renderer::kRuntimeAssetDiscoveryIntervalFrames;
};

class RendererRuntimeResourceStatsBuilder final {
public:
    RendererRuntimeResourceStatsBuilder() = delete;

    [[nodiscard]] static Renderer::RuntimeSceneResourceStats Build(const RendererRuntimeResourceStatsBuildDesc& desc) noexcept;
};

} // namespace kb::render
