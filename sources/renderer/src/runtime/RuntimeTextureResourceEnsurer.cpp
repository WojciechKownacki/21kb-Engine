#include "runtime/RuntimeTextureResourceEnsurer.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

void RuntimeTextureResourceEnsurer::Ensure(
    const RuntimeRenderResourceEnsureContext& context,
    const RuntimeMaterialResourceMap& materials,
    const RuntimeMaterialResourceMap& embeddedMaterials,
    RuntimeTextureResourceMap& textures) {
    kb::assets::AssetManager& manager = context.scene.Assets().Manager();

    auto ensureTexture = [&](std::uint64_t textureAssetId, RenderTextureColorSpace colorSpace) {
        if (textureAssetId == 0U) {
            return;
        }

        const RuntimeTextureAssetKey runtimeKey{
            .sceneId = context.scene.Id(),
            .assetId = textureAssetId,
            .colorSpace = colorSpace,
        };
        context.frameReferences.MarkTexture(runtimeKey);

        const kb::assets::AssetId assetId{ textureAssetId };
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetId);
        auto cacheIt = textures.find(runtimeKey);
        if (metadata == nullptr || metadata->type != "RenderTexture") {
            if (cacheIt != textures.end()) {
                context.sceneRenderer.ResourceMap().UnbindTextureHandle(cacheIt->second.handle);
                context.sceneRenderer.Resources().DestroyTexture(cacheIt->second.handle);
                static_cast<void>(manager.Unload(assetId));
                textures.erase(cacheIt);
            } else {
                context.sceneRenderer.ResourceMap().UnbindTexture(textureAssetId, colorSpace);
            }
            return;
        }

        if (cacheIt != textures.end() && cacheIt->second.contentHash == metadata->contentHash && context.sceneRenderer.Resources().ContainsTexture(cacheIt->second.handle)) {
            RuntimeTextureResource& cached = cacheIt->second;
            cached.lastReferencedFrame = context.currentFrame;
            context.sceneRenderer.ResourceMap().BindTexture(textureAssetId, colorSpace, cached.handle);
            return;
        }

        if (cacheIt != textures.end()) {
            context.sceneRenderer.ResourceMap().UnbindTextureHandle(cacheIt->second.handle);
            context.sceneRenderer.Resources().DestroyTexture(cacheIt->second.handle);
            static_cast<void>(manager.Unload(assetId));
            textures.erase(cacheIt);
        }

        const kb::assets::AssetHandle<RenderTextureAssetData> asset = manager.Load<RenderTextureAssetData>(assetId);
        if (!asset.IsLoaded() || asset->rgba8.empty()) {
            context.sceneRenderer.ResourceMap().UnbindTexture(textureAssetId, colorSpace);
            return;
        }

        const bgfx::Memory* memory = bgfx::copy(asset->rgba8.data(), static_cast<std::uint32_t>(asset->rgba8.size()));
        const RenderTextureHandle handle = context.sceneRenderer.Resources().RegisterTexture2D(asset->MakeDesc(memory, colorSpace));
        if (!handle.IsValid()) {
            context.sceneRenderer.ResourceMap().UnbindTexture(textureAssetId, colorSpace);
            static_cast<void>(manager.Unload(assetId));
            return;
        }

        textures[runtimeKey] = RuntimeTextureResource{
            .handle = handle,
            .contentHash = metadata->contentHash,
            .lastReferencedFrame = context.currentFrame,
        };
        context.sceneRenderer.ResourceMap().BindTexture(textureAssetId, colorSpace, handle);
    };

    for (const RuntimeAssetKey& materialKey : context.frameReferences.Materials()) {
        if (materialKey.sceneId != context.scene.Id()) {
            continue;
        }
        RenderMaterialHandle materialHandle{};
        if (const auto materialIt = materials.find(materialKey); materialIt != materials.end()) {
            materialHandle = materialIt->second.handle;
        } else if (const auto embeddedIt = embeddedMaterials.find(materialKey); embeddedIt != embeddedMaterials.end()) {
            materialHandle = embeddedIt->second.handle;
        } else {
            continue;
        }
        const RenderMaterialResource* material = context.sceneRenderer.Resources().FindMaterial(materialHandle);
        if (material == nullptr) {
            continue;
        }
        ensureTexture(material->albedoTextureAssetId, RenderTextureColorSpace::Srgb);
        ensureTexture(material->normalTextureAssetId, RenderTextureColorSpace::Linear);
        ensureTexture(material->metallicRoughnessTextureAssetId, RenderTextureColorSpace::Linear);
        ensureTexture(material->occlusionTextureAssetId, RenderTextureColorSpace::Linear);
        ensureTexture(material->emissiveTextureAssetId, RenderTextureColorSpace::Srgb);
    }
}

} // namespace kb::render
