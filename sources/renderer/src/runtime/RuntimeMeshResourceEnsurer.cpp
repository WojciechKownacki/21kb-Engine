#include "runtime/RuntimeMeshResourceEnsurer.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

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

} // namespace

void RuntimeMeshResourceEnsurer::Ensure(
    const RuntimeRenderResourceEnsureContext& context,
    RuntimeMeshResourceMap& meshes,
    RuntimeMaterialResourceMap& embeddedMaterials) {
    kb::assets::AssetManager& manager = context.scene.Assets().Manager();

    for (const auto& [entityId, proxy] : context.renderScene.MeshProxies()) {
        static_cast<void>(entityId);
        const std::uint64_t meshAssetId = proxy.desc.meshAssetId;
        if (meshAssetId == 0U) {
            continue;
        }
        const RuntimeAssetKey runtimeKey{
            .sceneId = context.scene.Id(),
            .assetId = meshAssetId,
        };
        context.frameReferences.MarkMesh(runtimeKey);

        const kb::assets::AssetId assetId{ meshAssetId };
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetId);
        auto cacheIt = meshes.find(runtimeKey);
        if (metadata == nullptr || metadata->type != "RenderMesh") {
            if (cacheIt != meshes.end()) {
                context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
                static_cast<void>(manager.Unload(assetId));
                meshes.erase(cacheIt);
            } else {
                context.sceneRenderer.ResourceMap().UnbindMesh(meshAssetId);
            }
            continue;
        }

        if (cacheIt != meshes.end() && cacheIt->second.contentHash == metadata->contentHash && context.sceneRenderer.Resources().ContainsMesh(cacheIt->second.handle)) {
            RuntimeMeshResource& cached = cacheIt->second;
            cached.lastReferencedFrame = context.currentFrame;
            context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, cached.handle);
            continue;
        }

        if (cacheIt != meshes.end()) {
            context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
            context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
            static_cast<void>(manager.Unload(assetId));
            meshes.erase(cacheIt);
        }

        const kb::assets::AssetHandle<RenderMeshAssetData> asset = manager.Load<RenderMeshAssetData>(assetId);
        if (!asset.IsLoaded()) {
            context.sceneRenderer.ResourceMap().UnbindMesh(meshAssetId);
            continue;
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
            continue;
        }

        meshes[runtimeKey] = RuntimeMeshResource{
            .handle = handle,
            .contentHash = metadata->contentHash,
            .lastReferencedFrame = context.currentFrame,
        };
        context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, handle);
    }
}

} // namespace kb::render
