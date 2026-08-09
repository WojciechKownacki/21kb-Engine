#include "runtime/RuntimeMeshResourceEnsurer.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "kb/render/resources/BuiltInParticleQuadMesh.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderAssetRefs.hpp"
#include "kb/render/resources/SkeletalMeshRenderResourceBuilder.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

#include <algorithm>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] std::uint64_t HashCombine(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return lhs ^ (rhs + 0x9e3779b97f4a7c15ULL + (lhs << 6U) + (lhs >> 2U));
}

void EmitUnresolvedMaterialTexturePathDiagnostic(
    SceneRenderDiagnostics& diagnostics,
    std::uint64_t meshAssetId,
    std::uint64_t materialAssetId,
    std::uint32_t unresolvedTexturePathCount) {
    if (unresolvedTexturePathCount == 0U) {
        return;
    }

    diagnostics.events.push_back(SceneRenderDiagnosticEvent{
        .severity = SceneRenderDiagnosticSeverity::Warning,
        .kind = SceneRenderDiagnosticKind::UnresolvedMaterialTexturePath,
        .meshAssetId = meshAssetId,
        .materialAssetId = materialAssetId,
        .instanceCount = unresolvedTexturePathCount,
    });
}

// LIB-143: the built-in particle billboard quad is never registered as a real
// kb::assets::AssetRegistry entry (unlike every other mesh this ensurer resolves) - it is
// content-immutable (never authored, never hot-reloaded), and AssetRegistry::assets_ is a
// std::vector<AssetMetadata>: any later kb::assets::AssetRegistry::Upsert/RegisterAsset call
// (elsewhere, for unrelated content) can reallocate that vector and invalidate every raw
// AssetMetadata* a caller obtained earlier (AssetRegistry::Find/FindByPath return pointers
// into that vector's storage, not independently heap-allocated objects like IAssetLoader's
// own unique_ptr-owned entries). Registering the built-in quad from within this ensurer -
// which SubmitScene calls on every frame's first render of a scene - would risk invalidating
// exactly that kind of pointer for any other code (test or production) that resolved an
// unrelated asset earlier in the same call chain. Resolving it here, entirely bypassing
// AssetRegistry/AssetManager::Load, removes that risk at the source rather than reducing its
// likelihood.
constexpr std::uint64_t kBuiltInParticleQuadMeshContentHash = 0x8371'C0DE'0001ULL;

[[nodiscard]] bool EnsureBuiltInParticleQuadMesh(
    const RuntimeRenderResourceEnsureContext& context,
    RuntimeMeshResourceMap& meshes,
    std::uint64_t meshAssetId,
    const RuntimeAssetKey& runtimeKey) {
    if (meshAssetId != BuiltInParticleQuadMeshAssetId().value) {
        return false;
    }

    const auto cacheIt = meshes.find(runtimeKey);
    if (cacheIt != meshes.end() && cacheIt->second.contentHash == kBuiltInParticleQuadMeshContentHash &&
        context.sceneRenderer.Resources().ContainsMesh(cacheIt->second.handle)) {
        RuntimeMeshResource& cached = cacheIt->second;
        cached.lastReferencedFrame = context.currentFrame;
        context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, cached.handle);
        return true;
    }

    if (cacheIt != meshes.end()) {
        context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
        context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
        meshes.erase(cacheIt);
    }

    const RenderMeshAssetData quad = BuildBuiltInParticleQuadMesh();
    const RenderMeshHandle handle = context.sceneRenderer.Resources().RegisterMesh(quad.desc);
    if (!handle.IsValid()) {
        context.sceneRenderer.ResourceMap().UnbindMesh(meshAssetId);
        return true;
    }

    meshes[runtimeKey] = RuntimeMeshResource{
        .handle = handle,
        .contentHash = kBuiltInParticleQuadMeshContentHash,
        .lastReferencedFrame = context.currentFrame,
    };
    context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, handle);
    return true;
}

} // namespace

void RuntimeMeshResourceEnsurer::Ensure(
    const RuntimeRenderResourceEnsureContext& context,
    RuntimeMeshResourceMap& meshes,
    RuntimeMaterialResourceMap& embeddedMaterials) {
    kb::assets::AssetManager& manager = context.scene.Assets().Manager();

    const auto ensureMorphMesh = [&](const MeshRenderProxyDesc& proxy) {
        if (!proxy.morphDeformationEnabled || proxy.skeletalMeshAssetId == 0U || proxy.meshAssetId == 0U) {
            return false;
        }
        const RuntimeAssetKey runtimeKey{
            .sceneId = context.scene.Id(),
            .assetId = proxy.meshAssetId,
        };
        context.frameReferences.MarkMesh(runtimeKey);
        const kb::assets::AssetId sourceAssetId{ proxy.skeletalMeshAssetId };
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(sourceAssetId);
        auto cacheIt = meshes.find(runtimeKey);
        if (metadata == nullptr || metadata->type != kb::scene::kSkeletalMeshAssetType) {
            if (cacheIt != meshes.end()) {
                context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
                meshes.erase(cacheIt);
            } else {
                context.sceneRenderer.ResourceMap().UnbindMesh(proxy.meshAssetId);
            }
            return true;
        }
        const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> asset =
            manager.Load<kb::scene::SkeletalMeshAsset>(sourceAssetId);
        if (!asset.IsLoaded()) {
            context.sceneRenderer.ResourceMap().UnbindMesh(proxy.meshAssetId);
            return true;
        }
        const kb::scene::DrawD3DeformedGeometryComponent* geometry =
            context.scene.Components().DeformedGeometries().TryGet(kb::scene::SceneEntity{ proxy.entityId });
        const kb::scene::SceneEntity poseSource = geometry != nullptr && geometry->poseSource.IsValid()
            ? geometry->poseSource : kb::scene::SceneEntity{ proxy.entityId };
        const std::optional<kb::scene::AnimatorInstanceSkeletonView> pose =
            context.scene.Animators().InstanceSkeleton(poseSource);
        const std::span<const std::string> morphTargetNames = pose
            ? pose->morphWeights.targetNames : std::span<const std::string>{};
        const std::span<const float> morphWeights = pose
            ? pose->morphWeights.currentWeights : std::span<const float>{};
        const std::optional<SkeletalMeshRenderResourceData> resource =
            SkeletalMeshRenderResourceBuilder::BuildValidated(*asset, morphTargetNames, morphWeights);
        if (!resource) {
            context.sceneRenderer.ResourceMap().UnbindMesh(proxy.meshAssetId);
            return true;
        }
        if (cacheIt != meshes.end() && cacheIt->second.contentHash == metadata->contentHash &&
            cacheIt->second.sourceAsset == asset.Get() &&
            context.sceneRenderer.Resources().ContainsMesh(cacheIt->second.handle) &&
            context.sceneRenderer.Resources().UpdateMeshVertices(
                cacheIt->second.handle, resource->desc, 0U, resource->desc.vertexCount)) {
            cacheIt->second.lastReferencedFrame = context.currentFrame;
            context.sceneRenderer.ResourceMap().BindMesh(proxy.meshAssetId, cacheIt->second.handle);
            return true;
        }
        if (cacheIt != meshes.end()) {
            context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
            context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
            meshes.erase(cacheIt);
        }
        const RenderMeshHandle handle = context.sceneRenderer.Resources().RegisterMesh(resource->desc);
        if (!handle.IsValid()) {
            context.sceneRenderer.ResourceMap().UnbindMesh(proxy.meshAssetId);
            return true;
        }
        meshes[runtimeKey] = RuntimeMeshResource{
            .handle = handle,
            .sourceAsset = asset.Get(),
            .sourceAssetId = sourceAssetId.value,
            .contentHash = metadata->contentHash,
            .lastReferencedFrame = context.currentFrame,
            .dynamicVertexUpdates = true,
        };
        context.sceneRenderer.ResourceMap().BindMesh(proxy.meshAssetId, handle);
        return true;
    };

    const auto ensureMesh = [&](std::uint64_t meshAssetId) {
        if (meshAssetId == 0U) {
            return;
        }
        const RuntimeAssetKey runtimeKey{
            .sceneId = context.scene.Id(),
            .assetId = meshAssetId,
        };
        context.frameReferences.MarkMesh(runtimeKey);

        if (EnsureBuiltInParticleQuadMesh(context, meshes, meshAssetId, runtimeKey)) {
            return;
        }

        const kb::assets::AssetId assetId{ meshAssetId };
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetId);
        auto cacheIt = meshes.find(runtimeKey);
        const bool skeletalMesh = metadata != nullptr && metadata->type == kb::scene::kSkeletalMeshAssetType;
        if (metadata == nullptr || (metadata->type != "RenderMesh" && !skeletalMesh)) {
            if (cacheIt != meshes.end()) {
                context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
                static_cast<void>(manager.Unload(assetId));
                meshes.erase(cacheIt);
            } else {
                context.sceneRenderer.ResourceMap().UnbindMesh(meshAssetId);
            }
            return;
        }

        if (cacheIt != meshes.end() && !cacheIt->second.dynamicVertexUpdates &&
            !cacheIt->second.dynamicTerrainLayerUpdates &&
            cacheIt->second.contentHash == metadata->contentHash &&
            context.sceneRenderer.Resources().ContainsMesh(cacheIt->second.handle)) {
            RuntimeMeshResource& cached = cacheIt->second;
            cached.lastReferencedFrame = context.currentFrame;
            context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, cached.handle);
            return;
        }

        if (skeletalMesh) {
            const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> asset =
                manager.Load<kb::scene::SkeletalMeshAsset>(assetId);
            if (!asset.IsLoaded()) {
                if (cacheIt != meshes.end()) {
                    context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
                    context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
                    meshes.erase(cacheIt);
                } else {
                    context.sceneRenderer.ResourceMap().UnbindMesh(meshAssetId);
                }
                return;
            }
            const std::optional<SkeletalMeshRenderResourceData> resource =
                SkeletalMeshRenderResourceBuilder::BuildValidated(*asset);
            if (!resource) {
                context.sceneRenderer.ResourceMap().UnbindMesh(meshAssetId);
                static_cast<void>(manager.Unload(assetId));
                return;
            }
            if (cacheIt != meshes.end()) {
                context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
                meshes.erase(cacheIt);
            }
            const RenderMeshHandle handle = context.sceneRenderer.Resources().RegisterMesh(resource->desc);
            if (!handle.IsValid()) {
                context.sceneRenderer.ResourceMap().UnbindMesh(meshAssetId);
                static_cast<void>(manager.Unload(assetId));
                return;
            }
            meshes[runtimeKey] = RuntimeMeshResource{
                .handle = handle,
                .sourceAsset = asset.Get(),
                .contentHash = metadata->contentHash,
                .lastReferencedFrame = context.currentFrame,
            };
            context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, handle);
            return;
        }

        const MeshRef asset = manager.Load<RenderMeshAssetData>(assetId);
        if (!asset.IsLoaded()) {
            if (cacheIt != meshes.end()) {
                context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
                meshes.erase(cacheIt);
            } else {
                context.sceneRenderer.ResourceMap().UnbindMesh(meshAssetId);
            }
            return;
        }

        // Terrain material painting mutates only the weight texture and per-layer active flags of the
        // editor-published working asset. Keep the existing geometry buffers and upload those subresources
        // directly. Pointer identity makes the metadata-only commit safe: a genuinely replaced asset still
        // falls through to the normal rebuild path.
        if (cacheIt != meshes.end() && cacheIt->second.sourceAsset == asset.Get() &&
            cacheIt->second.dynamicTopologyKey == asset->dynamicTopologyKey &&
            cacheIt->second.dynamicVertexUpdateCount == asset->dynamicVertexUpdateRanges.size() &&
            cacheIt->second.dynamicSectionUpdateCount <= asset->dynamicSectionUpdateIndices.size() &&
            cacheIt->second.dynamicTerrainLayerWeightUpdateCount <= asset->dynamicTerrainLayerWeightUpdates.size() &&
            context.sceneRenderer.Resources().ContainsMesh(cacheIt->second.handle)) {
            RuntimeMeshResource& cached = cacheIt->second;
            bool updated = true;
            std::vector<std::uint32_t> dirtySections{
                asset->dynamicSectionUpdateIndices.begin() + static_cast<std::ptrdiff_t>(cached.dynamicSectionUpdateCount),
                asset->dynamicSectionUpdateIndices.end(),
            };
            std::ranges::sort(dirtySections);
            dirtySections.erase(std::unique(dirtySections.begin(), dirtySections.end()), dirtySections.end());
            if (!dirtySections.empty()) {
                updated = context.sceneRenderer.Resources().UpdateMeshGeometryMetadata(
                    cached.handle, asset->desc, dirtySections);
            }
            if (updated && cached.dynamicTerrainLayerWeightUpdateCount <
                    asset->dynamicTerrainLayerWeightUpdates.size()) {
                RenderTerrainLayerWeightUpdateRegion merged =
                    asset->dynamicTerrainLayerWeightUpdates[cached.dynamicTerrainLayerWeightUpdateCount];
                std::uint32_t minX = merged.x;
                std::uint32_t minY = merged.y;
                std::uint32_t maxX = static_cast<std::uint32_t>(merged.x) + merged.width;
                std::uint32_t maxY = static_cast<std::uint32_t>(merged.y) + merged.height;
                for (std::size_t updateIndex = cached.dynamicTerrainLayerWeightUpdateCount + 1U;
                     updateIndex < asset->dynamicTerrainLayerWeightUpdates.size(); ++updateIndex) {
                    const RenderTerrainLayerWeightUpdateRegion& update =
                        asset->dynamicTerrainLayerWeightUpdates[updateIndex];
                    minX = std::min<std::uint32_t>(minX, update.x);
                    minY = std::min<std::uint32_t>(minY, update.y);
                    maxX = std::max<std::uint32_t>(maxX, static_cast<std::uint32_t>(update.x) + update.width);
                    maxY = std::max<std::uint32_t>(maxY, static_cast<std::uint32_t>(update.y) + update.height);
                }
                updated = context.sceneRenderer.Resources().UpdateMeshTerrainLayerWeights(
                    cached.handle,
                    asset->desc,
                    static_cast<std::uint16_t>(minX),
                    static_cast<std::uint16_t>(minY),
                    static_cast<std::uint16_t>(maxX - minX),
                    static_cast<std::uint16_t>(maxY - minY));
            }
            if (updated) {
                cached.dynamicTerrainLayerUpdates = true;
                cached.contentHash = metadata->contentHash;
                cached.dynamicSectionUpdateCount = asset->dynamicSectionUpdateIndices.size();
                cached.dynamicTerrainLayerWeightUpdateCount = asset->dynamicTerrainLayerWeightUpdates.size();
                cached.lastReferencedFrame = context.currentFrame;
                context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, cached.handle);
                return;
            }
        }

        if (cacheIt != meshes.end() && cacheIt->second.dynamicVertexUpdates &&
            asset->dynamicVertexUpdates && asset->dynamicTopologyKey != 0U &&
            cacheIt->second.dynamicTopologyKey == asset->dynamicTopologyKey &&
            cacheIt->second.dynamicVertexUpdateCount <= asset->dynamicVertexUpdateRanges.size() &&
            cacheIt->second.dynamicSectionUpdateCount <= asset->dynamicSectionUpdateIndices.size() &&
            cacheIt->second.dynamicTerrainLayerWeightUpdateCount <= asset->dynamicTerrainLayerWeightUpdates.size() &&
            context.sceneRenderer.Resources().ContainsMesh(cacheIt->second.handle)) {
            if (cacheIt->second.dynamicVertexUpdateCount == asset->dynamicVertexUpdateRanges.size() &&
                cacheIt->second.dynamicSectionUpdateCount == asset->dynamicSectionUpdateIndices.size() &&
                cacheIt->second.dynamicTerrainLayerWeightUpdateCount == asset->dynamicTerrainLayerWeightUpdates.size()) {
                cacheIt->second.contentHash = metadata->contentHash;
                cacheIt->second.lastReferencedFrame = context.currentFrame;
                context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, cacheIt->second.handle);
                return;
            }
            std::vector<RenderMeshVertexUpdateRange> pendingUpdates{
                asset->dynamicVertexUpdateRanges.begin() + static_cast<std::ptrdiff_t>(cacheIt->second.dynamicVertexUpdateCount),
                asset->dynamicVertexUpdateRanges.end(),
            };
            std::ranges::sort(pendingUpdates, {}, &RenderMeshVertexUpdateRange::firstVertex);
            std::size_t mergedCount = 0U;
            for (const RenderMeshVertexUpdateRange& update : pendingUpdates) {
                if (update.vertexCount == 0U) continue;
                if (mergedCount == 0U) {
                    pendingUpdates[mergedCount++] = update;
                    continue;
                }
                RenderMeshVertexUpdateRange& previous = pendingUpdates[mergedCount - 1U];
                const std::uint32_t previousEnd = previous.firstVertex + previous.vertexCount;
                const std::uint32_t updateEnd = update.firstVertex + update.vertexCount;
                if (update.firstVertex <= previousEnd) {
                    previous.vertexCount = std::max(previousEnd, updateEnd) - previous.firstVertex;
                } else {
                    pendingUpdates[mergedCount++] = update;
                }
            }
            bool updated = true;
            for (std::size_t updateIndex = 0U; updateIndex < mergedCount; ++updateIndex) {
                const RenderMeshVertexUpdateRange& update = pendingUpdates[updateIndex];
                if (!context.sceneRenderer.Resources().UpdateMeshVertices(
                        cacheIt->second.handle,
                        asset->desc,
                        update.firstVertex,
                        update.vertexCount)) {
                    updated = false;
                    break;
                }
            }
            std::vector<std::uint32_t> dirtySections{
                asset->dynamicSectionUpdateIndices.begin() + static_cast<std::ptrdiff_t>(cacheIt->second.dynamicSectionUpdateCount),
                asset->dynamicSectionUpdateIndices.end(),
            };
            std::ranges::sort(dirtySections);
            dirtySections.erase(std::unique(dirtySections.begin(), dirtySections.end()), dirtySections.end());
            if (!dirtySections.empty()) {
                updated = updated && context.sceneRenderer.Resources().UpdateMeshGeometryMetadata(
                    cacheIt->second.handle, asset->desc, dirtySections);
            }
            if (updated && cacheIt->second.dynamicTerrainLayerWeightUpdateCount <
                    asset->dynamicTerrainLayerWeightUpdates.size()) {
                RenderTerrainLayerWeightUpdateRegion merged =
                    asset->dynamicTerrainLayerWeightUpdates[cacheIt->second.dynamicTerrainLayerWeightUpdateCount];
                std::uint32_t minX = merged.x;
                std::uint32_t minY = merged.y;
                std::uint32_t maxX = static_cast<std::uint32_t>(merged.x) + merged.width;
                std::uint32_t maxY = static_cast<std::uint32_t>(merged.y) + merged.height;
                for (std::size_t updateIndex = cacheIt->second.dynamicTerrainLayerWeightUpdateCount + 1U;
                     updateIndex < asset->dynamicTerrainLayerWeightUpdates.size(); ++updateIndex) {
                    const RenderTerrainLayerWeightUpdateRegion& update =
                        asset->dynamicTerrainLayerWeightUpdates[updateIndex];
                    minX = std::min<std::uint32_t>(minX, update.x);
                    minY = std::min<std::uint32_t>(minY, update.y);
                    maxX = std::max<std::uint32_t>(maxX, static_cast<std::uint32_t>(update.x) + update.width);
                    maxY = std::max<std::uint32_t>(maxY, static_cast<std::uint32_t>(update.y) + update.height);
                }
                updated = context.sceneRenderer.Resources().UpdateMeshTerrainLayerWeights(
                    cacheIt->second.handle,
                    asset->desc,
                    static_cast<std::uint16_t>(minX),
                    static_cast<std::uint16_t>(minY),
                    static_cast<std::uint16_t>(maxX - minX),
                    static_cast<std::uint16_t>(maxY - minY));
            }
            if (updated) {
                cacheIt->second.sourceAsset = asset.Get();
                cacheIt->second.contentHash = metadata->contentHash;
                cacheIt->second.dynamicVertexUpdateCount = asset->dynamicVertexUpdateRanges.size();
                cacheIt->second.dynamicSectionUpdateCount = asset->dynamicSectionUpdateIndices.size();
                cacheIt->second.dynamicTerrainLayerWeightUpdateCount = asset->dynamicTerrainLayerWeightUpdates.size();
                cacheIt->second.lastReferencedFrame = context.currentFrame;
                context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, cacheIt->second.handle);
                return;
            }
        }

        if (cacheIt != meshes.end()) {
            context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
            context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
            meshes.erase(cacheIt);
        }

        std::vector<RenderMaterialSlotDesc> materialSlots = asset->materialSlots;
        for (std::uint32_t slotIndex = 0U; slotIndex < materialSlots.size() && slotIndex < asset->embeddedMaterials.size(); ++slotIndex) {
            if (materialSlots[slotIndex].defaultMaterialAssetId != 0U) {
                continue;
            }

            const RenderMeshEmbeddedMaterial& embeddedMaterial = asset->embeddedMaterials[slotIndex];
            const std::uint64_t embeddedMaterialAssetId = RuntimeMaterialResolver::EmbeddedMaterialAssetId(meshAssetId, slotIndex, embeddedMaterial.name);
            if (embeddedMaterialAssetId == 0U) {
                continue;
            }

            materialSlots[slotIndex].defaultMaterialAssetId = embeddedMaterialAssetId;
            const RuntimeAssetKey embeddedKey{
                .sceneId = context.scene.Id(),
                .assetId = embeddedMaterialAssetId,
            };
            context.frameReferences.MarkMaterial(embeddedKey);
            const std::uint64_t embeddedContentHash = HashCombine(metadata->contentHash, static_cast<std::uint64_t>(slotIndex));
            auto embeddedIt = embeddedMaterials.find(embeddedKey);
            if (embeddedIt != embeddedMaterials.end() &&
                embeddedIt->second.contentHash == embeddedContentHash &&
                context.sceneRenderer.Resources().ContainsMaterial(embeddedIt->second.handle)) {
                embeddedIt->second.lastReferencedFrame = context.currentFrame;
                context.sceneRenderer.ResourceMap().BindMaterial(embeddedMaterialAssetId, embeddedIt->second.handle);
                continue;
            }

            if (embeddedIt != embeddedMaterials.end()) {
                context.sceneRenderer.ResourceMap().UnbindMaterialHandle(embeddedIt->second.handle);
                context.sceneRenderer.Resources().DestroyMaterial(embeddedIt->second.handle);
                embeddedMaterials.erase(embeddedIt);
            }

            const ResolvedRuntimeMaterialDesc embeddedDesc = context.materialResolver.ResolveEmbeddedMaterial(manager, *metadata, embeddedMaterial);
            context.unresolvedMaterialTexturePathCount += embeddedDesc.unresolvedTexturePathCount;
            EmitUnresolvedMaterialTexturePathDiagnostic(context.diagnostics, meshAssetId, embeddedMaterialAssetId, embeddedDesc.unresolvedTexturePathCount);
            const RenderMaterialHandle embeddedHandle = context.sceneRenderer.Resources().RegisterMaterial(embeddedDesc.desc);
            if (!embeddedHandle.IsValid()) {
                context.sceneRenderer.ResourceMap().UnbindMaterial(embeddedMaterialAssetId);
                materialSlots[slotIndex].defaultMaterialAssetId = 0U;
                continue;
            }

            embeddedMaterials[embeddedKey] = RuntimeMaterialResource{
                .handle = embeddedHandle,
                .contentHash = embeddedContentHash,
                .lastReferencedFrame = context.currentFrame,
            };
            context.sceneRenderer.ResourceMap().BindMaterial(embeddedMaterialAssetId, embeddedHandle);
        }

        RenderMeshDesc desc = asset->desc;
        if (!materialSlots.empty()) {
            desc.materialSlots = materialSlots.data();
            desc.materialSlotCount = static_cast<std::uint32_t>(materialSlots.size());
        }
        const RenderMeshHandle handle = context.sceneRenderer.Resources().RegisterMesh(desc);
        if (!handle.IsValid()) {
            context.sceneRenderer.ResourceMap().UnbindMesh(meshAssetId);
            static_cast<void>(manager.Unload(assetId));
            return;
        }

        meshes[runtimeKey] = RuntimeMeshResource{
            .handle = handle,
            .sourceAsset = asset.Get(),
            .contentHash = metadata->contentHash,
            .dynamicTopologyKey = asset->dynamicTopologyKey,
            .dynamicVertexUpdateCount = asset->dynamicVertexUpdateRanges.size(),
            .dynamicSectionUpdateCount = asset->dynamicSectionUpdateIndices.size(),
            .dynamicTerrainLayerWeightUpdateCount = asset->dynamicTerrainLayerWeightUpdates.size(),
            .lastReferencedFrame = context.currentFrame,
            .dynamicVertexUpdates = asset->dynamicVertexUpdates,
            .dynamicTerrainLayerUpdates = !asset->dynamicTerrainLayerWeightUpdates.empty(),
        };
        context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, handle);
    };

    for (const auto& [entityId, proxy] : context.renderScene.MeshProxies()) {
        static_cast<void>(entityId);
        if (!ensureMorphMesh(proxy.desc)) ensureMesh(proxy.desc.meshAssetId);
    }
    for (const auto& [entityId, proxy] : context.renderScene.GeometrySwarmProxies()) {
        static_cast<void>(entityId);
        ensureMesh(proxy.desc.meshAssetId);
    }
}

} // namespace kb::render
