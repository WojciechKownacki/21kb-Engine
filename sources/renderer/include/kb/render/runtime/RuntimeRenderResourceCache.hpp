#pragma once

#include "kb/render/runtime/RuntimeFrameResourceReferences.hpp"
#include "kb/render/runtime/RuntimeRenderResourceCacheTypes.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>

namespace kb::scene {
class Scene;
}

namespace kb::render {

class RenderScene;
class RuntimeMaterialResolver;
class RuntimeRenderAssetDiscovery;
class SceneRenderer;
struct SceneRenderDiagnostics;

struct RuntimeRenderResourceCacheReserveDesc {
    std::uint32_t meshes = 0;
    std::uint32_t materials = 0;
    std::uint32_t textures = 0;
};

struct RuntimeRenderResourceCacheStats {
    std::uint32_t meshCount = 0;
    std::uint32_t materialCount = 0;
    std::uint32_t textureCount = 0;
    std::uint32_t meshCapacity = 0;
    std::uint32_t materialCapacity = 0;
    std::uint32_t textureCapacity = 0;
};

struct RuntimeRenderResourceEnsureContext {
    kb::scene::Scene& scene;
    const RenderScene& renderScene;
    SceneRenderer& sceneRenderer;
    RuntimeRenderAssetDiscovery& assetDiscovery;
    RuntimeFrameResourceReferences& frameReferences;
    RuntimeMaterialResolver& materialResolver;
    SceneRenderDiagnostics& diagnostics;
    std::uint32_t& unresolvedMaterialTexturePathCount;
    std::uint32_t& defaultMaterialFallbackCount;
    std::uint32_t& errorMaterialFallbackCount;
    std::uint32_t& materialLoadedCount;
    std::uint32_t& materialFallbackCount;
    std::uint32_t& materialErrorCount;
    std::uint32_t& materialReloadCount;
    std::uint32_t& materialResolverDiagnosticCount;
    std::uint32_t& graphMaterialCpuFallbackCount;
    std::uint32_t& graphMaterialGpuCount;
    std::uint64_t currentFrame = 0;
};

using RuntimeMeshResourceMap = std::unordered_map<RuntimeAssetKey, RuntimeMeshResource, RuntimeAssetKeyHash>;
using RuntimeMaterialResourceMap = std::unordered_map<RuntimeAssetKey, RuntimeMaterialResource, RuntimeAssetKeyHash>;
using RuntimeTextureResourceMap = std::unordered_map<RuntimeTextureAssetKey, RuntimeTextureResource, RuntimeTextureAssetKeyHash>;

class RuntimeRenderResourceCache {
public:
    void Reserve(const RuntimeRenderResourceCacheReserveDesc& desc);
    void EnsureSceneResources(const RuntimeRenderResourceEnsureContext& context);
    void ReleaseScene(kb::scene::Scene& scene, SceneRenderer* sceneRenderer) noexcept;
    void DestroyAll(SceneRenderer* sceneRenderer) noexcept;
    void InvalidateMaterials(SceneRenderer* sceneRenderer) noexcept;
    void PruneUnused(
        std::span<const kb::scene::Scene*> submittedScenes,
        const RuntimeFrameResourceReferences& frameReferences,
        SceneRenderer& sceneRenderer,
        std::uint64_t currentFrame,
        std::uint64_t retentionFrames);

    [[nodiscard]] RuntimeRenderResourceCacheStats Stats() const noexcept;

private:
    RuntimeMeshResourceMap meshes_;
    RuntimeMaterialResourceMap materials_;
    RuntimeMaterialResourceMap embeddedMaterials_;
    RuntimeTextureResourceMap textures_;

    void EnsureMeshResources(const RuntimeRenderResourceEnsureContext& context);
    void EnsureMaterialResources(const RuntimeRenderResourceEnsureContext& context);
    void EnsureTextureResources(const RuntimeRenderResourceEnsureContext& context);
};

} // namespace kb::render
