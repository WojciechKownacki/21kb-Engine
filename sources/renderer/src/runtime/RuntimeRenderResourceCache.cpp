#include "kb/render/runtime/RuntimeRenderResourceCache.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "kb/render/runtime/RuntimeRenderAssetDiscovery.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

#include <bgfx/bgfx.h>

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

void RuntimeRenderResourceCache::Reserve(const RuntimeRenderResourceCacheReserveDesc& desc) {
    if (desc.meshes > 0U) {
        meshes_.reserve(desc.meshes);
    }
    if (desc.materials > 0U) {
        materials_.reserve(desc.materials);
        embeddedMaterials_.reserve(desc.materials);
    }
    if (desc.textures > 0U) {
        textures_.reserve(desc.textures);
    }
}

void RuntimeRenderResourceCache::EnsureSceneResources(const RuntimeRenderResourceEnsureContext& context) {
    context.assetDiscovery.Ensure(context.scene, context.currentFrame);
    EnsureMeshResources(context);
    EnsureMaterialResources(context);
    EnsureTextureResources(context);
}

void RuntimeRenderResourceCache::EnsureMeshResources(const RuntimeRenderResourceEnsureContext& context) {
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
        auto cacheIt = meshes_.find(runtimeKey);
        if (metadata == nullptr || metadata->type != "RenderMesh") {
            if (cacheIt != meshes_.end()) {
                context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
                static_cast<void>(manager.Unload(assetId));
                meshes_.erase(cacheIt);
            } else {
                context.sceneRenderer.ResourceMap().UnbindMesh(meshAssetId);
            }
            continue;
        }

        if (cacheIt != meshes_.end() && cacheIt->second.contentHash == metadata->contentHash && context.sceneRenderer.Resources().ContainsMesh(cacheIt->second.handle)) {
            RuntimeMeshResource& cached = cacheIt->second;
            cached.lastReferencedFrame = context.currentFrame;
            context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, cached.handle);
            continue;
        }

        if (cacheIt != meshes_.end()) {
            context.sceneRenderer.ResourceMap().UnbindMeshHandle(cacheIt->second.handle);
            context.sceneRenderer.Resources().DestroyMesh(cacheIt->second.handle);
            static_cast<void>(manager.Unload(assetId));
            meshes_.erase(cacheIt);
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
            auto embeddedIt = embeddedMaterials_.find(embeddedKey);
            if (embeddedIt != embeddedMaterials_.end() &&
                embeddedIt->second.contentHash == embeddedContentHash &&
                context.sceneRenderer.Resources().ContainsMaterial(embeddedIt->second.handle)) {
                embeddedIt->second.lastReferencedFrame = context.currentFrame;
                context.sceneRenderer.ResourceMap().BindMaterial(embeddedMaterialAssetId, embeddedIt->second.handle);
                continue;
            }

            if (embeddedIt != embeddedMaterials_.end()) {
                context.sceneRenderer.ResourceMap().UnbindMaterialHandle(embeddedIt->second.handle);
                context.sceneRenderer.Resources().DestroyMaterial(embeddedIt->second.handle);
                embeddedMaterials_.erase(embeddedIt);
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

            embeddedMaterials_[embeddedKey] = RuntimeMaterialResource{
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

        meshes_[runtimeKey] = RuntimeMeshResource{
            .handle = handle,
            .contentHash = metadata->contentHash,
            .lastReferencedFrame = context.currentFrame,
        };
        context.sceneRenderer.ResourceMap().BindMesh(meshAssetId, handle);
    }
}

void RuntimeRenderResourceCache::EnsureMaterialResources(const RuntimeRenderResourceEnsureContext& context) {
    kb::assets::AssetManager& manager = context.scene.Assets().Manager();

    auto ensureMaterial = [&](std::uint64_t materialAssetId) {
        if (materialAssetId == 0U) {
            return;
        }

        const RuntimeAssetKey runtimeKey{
            .sceneId = context.scene.Id(),
            .assetId = materialAssetId,
        };
        context.frameReferences.MarkMaterial(runtimeKey);

        const auto embeddedIt = embeddedMaterials_.find(runtimeKey);
        if (embeddedIt != embeddedMaterials_.end()) {
            if (context.sceneRenderer.Resources().ContainsMaterial(embeddedIt->second.handle)) {
                RuntimeMaterialResource& cached = embeddedIt->second;
                cached.lastReferencedFrame = context.currentFrame;
                context.sceneRenderer.ResourceMap().BindMaterial(materialAssetId, cached.handle);
                return;
            }
            context.sceneRenderer.ResourceMap().UnbindMaterialHandle(embeddedIt->second.handle);
            embeddedMaterials_.erase(embeddedIt);
            context.sceneRenderer.ResourceMap().UnbindMaterial(materialAssetId);
            return;
        }

        const kb::assets::AssetId assetId{ materialAssetId };
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetId);
        auto cacheIt = materials_.find(runtimeKey);
        if (metadata == nullptr || metadata->type != "RenderMaterial") {
            if (cacheIt != materials_.end()) {
                context.sceneRenderer.ResourceMap().UnbindMaterialHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyMaterial(cacheIt->second.handle);
                static_cast<void>(manager.Unload(assetId));
                materials_.erase(cacheIt);
            } else {
                context.sceneRenderer.ResourceMap().UnbindMaterial(materialAssetId);
            }
            return;
        }

        if (cacheIt != materials_.end() && cacheIt->second.contentHash == metadata->contentHash && context.sceneRenderer.Resources().ContainsMaterial(cacheIt->second.handle)) {
            RuntimeMaterialResource& cached = cacheIt->second;
            cached.lastReferencedFrame = context.currentFrame;
            context.sceneRenderer.ResourceMap().BindMaterial(materialAssetId, cached.handle);
            return;
        }

        if (cacheIt != materials_.end()) {
            context.sceneRenderer.ResourceMap().UnbindMaterialHandle(cacheIt->second.handle);
            context.sceneRenderer.Resources().DestroyMaterial(cacheIt->second.handle);
            static_cast<void>(manager.Unload(assetId));
            materials_.erase(cacheIt);
        }

        const kb::assets::AssetHandle<RenderMaterialAssetData> asset = manager.Load<RenderMaterialAssetData>(assetId);
        if (!asset.IsLoaded()) {
            context.sceneRenderer.ResourceMap().UnbindMaterial(materialAssetId);
            return;
        }

        const ResolvedRuntimeMaterialDesc materialDesc = context.materialResolver.ResolveLoadedMaterial(manager, *metadata, *asset);
        context.unresolvedMaterialTexturePathCount += materialDesc.unresolvedTexturePathCount;
        EmitUnresolvedMaterialTexturePathDiagnostic(context.diagnostics, 0U, materialAssetId, materialDesc.unresolvedTexturePathCount);
        const RenderMaterialHandle handle = context.sceneRenderer.Resources().RegisterMaterial(materialDesc.desc);
        if (!handle.IsValid()) {
            context.sceneRenderer.ResourceMap().UnbindMaterial(materialAssetId);
            static_cast<void>(manager.Unload(assetId));
            return;
        }

        materials_[runtimeKey] = RuntimeMaterialResource{
            .handle = handle,
            .contentHash = metadata->contentHash,
            .lastReferencedFrame = context.currentFrame,
        };
        context.sceneRenderer.ResourceMap().BindMaterial(materialAssetId, handle);
    };

    for (const auto& [entityId, proxy] : context.renderScene.MeshProxies()) {
        static_cast<void>(entityId);
        ensureMaterial(proxy.desc.materialAssetId);
        for (std::uint32_t slotIndex = 0U; slotIndex < proxy.desc.materialSlotOverrideCount && slotIndex < kMaxSceneMaterialSlotOverrides; ++slotIndex) {
            ensureMaterial(proxy.desc.materialSlotAssetIds[slotIndex]);
        }

        const RenderMeshHandle meshHandle = context.sceneRenderer.ResourceMap().ResolveMesh(proxy.desc.meshAssetId);
        const RenderMeshResource* meshResource = context.sceneRenderer.Resources().FindMesh(meshHandle);
        if (meshResource == nullptr) {
            continue;
        }
        for (const RenderMaterialSlot& slot : meshResource->materialSlots) {
            ensureMaterial(slot.defaultMaterialAssetId);
        }
    }
}

void RuntimeRenderResourceCache::EnsureTextureResources(const RuntimeRenderResourceEnsureContext& context) {
    kb::assets::AssetManager& manager = context.scene.Assets().Manager();

    auto ensureTexture = [&](std::uint64_t textureAssetId) {
        if (textureAssetId == 0U) {
            return;
        }

        const RuntimeAssetKey runtimeKey{
            .sceneId = context.scene.Id(),
            .assetId = textureAssetId,
        };
        context.frameReferences.MarkTexture(runtimeKey);

        const kb::assets::AssetId assetId{ textureAssetId };
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetId);
        auto cacheIt = textures_.find(runtimeKey);
        if (metadata == nullptr || metadata->type != "RenderTexture") {
            if (cacheIt != textures_.end()) {
                context.sceneRenderer.ResourceMap().UnbindTextureHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyTexture(cacheIt->second.handle);
                static_cast<void>(manager.Unload(assetId));
                textures_.erase(cacheIt);
            } else {
                context.sceneRenderer.ResourceMap().UnbindTexture(textureAssetId);
            }
            return;
        }

        if (cacheIt != textures_.end() && cacheIt->second.contentHash == metadata->contentHash && context.sceneRenderer.Resources().ContainsTexture(cacheIt->second.handle)) {
            RuntimeTextureResource& cached = cacheIt->second;
            cached.lastReferencedFrame = context.currentFrame;
            context.sceneRenderer.ResourceMap().BindTexture(textureAssetId, cached.handle);
            return;
        }

        if (cacheIt != textures_.end()) {
            context.sceneRenderer.ResourceMap().UnbindTextureHandle(cacheIt->second.handle);
            context.sceneRenderer.Resources().DestroyTexture(cacheIt->second.handle);
            static_cast<void>(manager.Unload(assetId));
            textures_.erase(cacheIt);
        }

        const kb::assets::AssetHandle<RenderTextureAssetData> asset = manager.Load<RenderTextureAssetData>(assetId);
        if (!asset.IsLoaded() || asset->rgba8.empty()) {
            context.sceneRenderer.ResourceMap().UnbindTexture(textureAssetId);
            return;
        }

        const bgfx::Memory* memory = bgfx::copy(asset->rgba8.data(), static_cast<std::uint32_t>(asset->rgba8.size()));
        const RenderTextureHandle handle = context.sceneRenderer.Resources().RegisterTexture2D(asset->MakeDesc(memory));
        if (!handle.IsValid()) {
            context.sceneRenderer.ResourceMap().UnbindTexture(textureAssetId);
            static_cast<void>(manager.Unload(assetId));
            return;
        }

        textures_[runtimeKey] = RuntimeTextureResource{
            .handle = handle,
            .contentHash = metadata->contentHash,
            .lastReferencedFrame = context.currentFrame,
        };
        context.sceneRenderer.ResourceMap().BindTexture(textureAssetId, handle);
    };

    for (const RuntimeAssetKey& materialKey : context.frameReferences.Materials()) {
        if (materialKey.sceneId != context.scene.Id()) {
            continue;
        }
        RenderMaterialHandle materialHandle{};
        if (const auto materialIt = materials_.find(materialKey); materialIt != materials_.end()) {
            materialHandle = materialIt->second.handle;
        } else if (const auto embeddedIt = embeddedMaterials_.find(materialKey); embeddedIt != embeddedMaterials_.end()) {
            materialHandle = embeddedIt->second.handle;
        } else {
            continue;
        }
        const RenderMaterialResource* material = context.sceneRenderer.Resources().FindMaterial(materialHandle);
        if (material == nullptr) {
            continue;
        }
        ensureTexture(material->albedoTextureAssetId);
        ensureTexture(material->normalTextureAssetId);
        ensureTexture(material->metallicRoughnessTextureAssetId);
        ensureTexture(material->occlusionTextureAssetId);
        ensureTexture(material->emissiveTextureAssetId);
    }
}

void RuntimeRenderResourceCache::ReleaseScene(kb::scene::Scene& scene, SceneRenderer* sceneRenderer) noexcept {
    if (sceneRenderer == nullptr) {
        return;
    }

    kb::assets::AssetManager& manager = scene.Assets().Manager();
    const std::uint64_t sceneId = scene.Id();
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (it->first.sceneId != sceneId) {
            ++it;
            continue;
        }
        sceneRenderer->ResourceMap().UnbindMeshHandle(it->second.handle);
        sceneRenderer->Resources().DestroyMesh(it->second.handle);
        static_cast<void>(manager.Unload(kb::assets::AssetId{ it->first.assetId }));
        it = meshes_.erase(it);
    }
    for (auto it = materials_.begin(); it != materials_.end();) {
        if (it->first.sceneId != sceneId) {
            ++it;
            continue;
        }
        sceneRenderer->ResourceMap().UnbindMaterialHandle(it->second.handle);
        sceneRenderer->Resources().DestroyMaterial(it->second.handle);
        static_cast<void>(manager.Unload(kb::assets::AssetId{ it->first.assetId }));
        it = materials_.erase(it);
    }
    for (auto it = embeddedMaterials_.begin(); it != embeddedMaterials_.end();) {
        if (it->first.sceneId != sceneId) {
            ++it;
            continue;
        }
        sceneRenderer->ResourceMap().UnbindMaterialHandle(it->second.handle);
        sceneRenderer->Resources().DestroyMaterial(it->second.handle);
        it = embeddedMaterials_.erase(it);
    }
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (it->first.sceneId != sceneId) {
            ++it;
            continue;
        }
        sceneRenderer->ResourceMap().UnbindTextureHandle(it->second.handle);
        sceneRenderer->Resources().DestroyTexture(it->second.handle);
        static_cast<void>(manager.Unload(kb::assets::AssetId{ it->first.assetId }));
        it = textures_.erase(it);
    }
}

void RuntimeRenderResourceCache::DestroyAll(SceneRenderer* sceneRenderer) noexcept {
    if (sceneRenderer == nullptr) {
        meshes_.clear();
        materials_.clear();
        embeddedMaterials_.clear();
        textures_.clear();
        return;
    }

    for (const auto& [meshKey, resource] : meshes_) {
        static_cast<void>(meshKey);
        sceneRenderer->ResourceMap().UnbindMeshHandle(resource.handle);
        sceneRenderer->Resources().DestroyMesh(resource.handle);
    }
    meshes_.clear();

    for (const auto& [materialKey, resource] : materials_) {
        static_cast<void>(materialKey);
        sceneRenderer->ResourceMap().UnbindMaterialHandle(resource.handle);
        sceneRenderer->Resources().DestroyMaterial(resource.handle);
    }
    materials_.clear();

    for (const auto& [materialKey, resource] : embeddedMaterials_) {
        static_cast<void>(materialKey);
        sceneRenderer->ResourceMap().UnbindMaterialHandle(resource.handle);
        sceneRenderer->Resources().DestroyMaterial(resource.handle);
    }
    embeddedMaterials_.clear();

    for (const auto& [textureKey, resource] : textures_) {
        static_cast<void>(textureKey);
        sceneRenderer->ResourceMap().UnbindTextureHandle(resource.handle);
        sceneRenderer->Resources().DestroyTexture(resource.handle);
    }
    textures_.clear();
}

void RuntimeRenderResourceCache::PruneUnused(
    std::span<const kb::scene::Scene*> submittedScenes,
    const RuntimeFrameResourceReferences& frameReferences,
    SceneRenderer& sceneRenderer,
    std::uint64_t currentFrame,
    std::uint64_t retentionFrames) {
    auto unloadFromSubmittedScene = [&](RuntimeAssetKey key) {
        for (const kb::scene::Scene* scene : submittedScenes) {
            if (scene != nullptr && scene->Id() == key.sceneId) {
                static_cast<void>(const_cast<kb::scene::Scene*>(scene)->Assets().Manager().Unload(kb::assets::AssetId{ key.assetId }));
                break;
            }
        }
    };

    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (frameReferences.ContainsMesh(it->first) ||
            currentFrame <= it->second.lastReferencedFrame + retentionFrames) {
            ++it;
            continue;
        }
        sceneRenderer.ResourceMap().UnbindMeshHandle(it->second.handle);
        sceneRenderer.Resources().DestroyMesh(it->second.handle);
        unloadFromSubmittedScene(it->first);
        it = meshes_.erase(it);
    }

    for (auto it = materials_.begin(); it != materials_.end();) {
        if (frameReferences.ContainsMaterial(it->first) ||
            currentFrame <= it->second.lastReferencedFrame + retentionFrames) {
            ++it;
            continue;
        }
        sceneRenderer.ResourceMap().UnbindMaterialHandle(it->second.handle);
        sceneRenderer.Resources().DestroyMaterial(it->second.handle);
        unloadFromSubmittedScene(it->first);
        it = materials_.erase(it);
    }

    for (auto it = embeddedMaterials_.begin(); it != embeddedMaterials_.end();) {
        if (frameReferences.ContainsMaterial(it->first) ||
            currentFrame <= it->second.lastReferencedFrame + retentionFrames) {
            ++it;
            continue;
        }
        sceneRenderer.ResourceMap().UnbindMaterialHandle(it->second.handle);
        sceneRenderer.Resources().DestroyMaterial(it->second.handle);
        it = embeddedMaterials_.erase(it);
    }

    for (auto it = textures_.begin(); it != textures_.end();) {
        if (frameReferences.ContainsTexture(it->first) ||
            currentFrame <= it->second.lastReferencedFrame + retentionFrames) {
            ++it;
            continue;
        }
        sceneRenderer.ResourceMap().UnbindTextureHandle(it->second.handle);
        sceneRenderer.Resources().DestroyTexture(it->second.handle);
        unloadFromSubmittedScene(it->first);
        it = textures_.erase(it);
    }
}

RuntimeRenderResourceCacheStats RuntimeRenderResourceCache::Stats() const noexcept {
    return RuntimeRenderResourceCacheStats{
        .meshCount = static_cast<std::uint32_t>(meshes_.size()),
        .materialCount = static_cast<std::uint32_t>(materials_.size() + embeddedMaterials_.size()),
        .textureCount = static_cast<std::uint32_t>(textures_.size()),
        .meshCapacity = static_cast<std::uint32_t>(meshes_.bucket_count()),
        .materialCapacity = static_cast<std::uint32_t>(materials_.bucket_count() + embeddedMaterials_.bucket_count()),
        .textureCapacity = static_cast<std::uint32_t>(textures_.bucket_count()),
    };
}

} // namespace kb::render
