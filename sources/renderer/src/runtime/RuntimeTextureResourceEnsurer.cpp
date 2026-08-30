#include "runtime/RuntimeTextureResourceEnsurer.hpp"
#include "runtime/RuntimeTextureMipChain.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderTextureAssetLoader.hpp"
#include "kb/render/resources/RenderAssetRefs.hpp"
#include "kb/render/resources/RenderMaterialTextureSlots.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <bgfx/bgfx.h>

#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace kb::render {
namespace {

[[nodiscard]] bool IsRuntimeTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderTexture" || metadata.type == "Texture" || metadata.importCategory == "Texture";
}

[[nodiscard]] std::filesystem::path ResolveAssetPhysicalPath(const kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata) {
    if (!metadata.physicalPath.empty()) {
        return metadata.physicalPath;
    }
    return manager.Mounts().Resolve(metadata.virtualPath).value_or(std::filesystem::path{});
}

} // namespace

void RuntimeTextureResourceEnsurer::Ensure(
    const RuntimeRenderResourceEnsureContext& context,
    const RuntimeMaterialResourceMap& materials,
    const RuntimeMaterialResourceMap& embeddedMaterials,
    RuntimeTextureResourceMap& textures) {
    kb::assets::AssetManager& manager = context.scene.Assets().Manager();

    auto ensureTexture = [&](
                             std::uint64_t textureAssetId,
                             RenderTextureColorSpace colorSpace,
                             RenderTextureDimension expectedDimension) {
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
        if (metadata == nullptr || !IsRuntimeTextureAsset(*metadata)) {
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
            if (cached.dimension != expectedDimension) {
                std::ostringstream row;
                row << "texture-dimension-mismatch assetId=" << textureAssetId
                    << " expected=" << RenderTextureDimensionName(expectedDimension)
                    << " actual=" << RenderTextureDimensionName(cached.dimension);
                WriteRendererMaterialGraphDebugLog("resource", row.str());
            }
            return;
        }

        if (cacheIt != textures.end()) {
            context.sceneRenderer.ResourceMap().UnbindTextureHandle(cacheIt->second.handle);
            context.sceneRenderer.Resources().DestroyTexture(cacheIt->second.handle);
            static_cast<void>(manager.Unload(assetId));
            textures.erase(cacheIt);
        }

        const std::filesystem::path texturePath = ResolveAssetPhysicalPath(manager, *metadata);
        // When streaming is enabled (editor app), bind the texture only once it is decoded: on the first
        // reference the decode is queued on a background worker and the slot is left unbound for now, so picking
        // a texture / opening a material no longer freezes the render thread ~1s on a large image - the texture
        // streams in a frame or two later. When disabled (tests, headless captures) decode synchronously so a
        // texture is deterministically bound in the same submit.
        std::shared_ptr<const RenderTextureAssetData> asset;
        if (!texturePath.empty()) {
            if (RenderTextureAssetLoader::IsAsyncTextureDecodeEnabled()) {
                asset = RenderTextureAssetLoader::TryAcquireDecodedTexture(texturePath);
                if (asset == nullptr) {
                    RenderTextureAssetLoader::RequestAsyncTextureDecode(texturePath);
                }
            } else if (std::optional<RenderTextureAssetData> decoded = RenderTextureAssetLoader::LoadTexture(texturePath);
                       decoded.has_value()) {
                asset = std::make_shared<const RenderTextureAssetData>(std::move(*decoded));
            }
        }
        // A baked texture arrives in a GPU block format with its mip chain already built. The
        // device decides whether it stays that way: bgfx reports per-format support in
        // getCaps(), and handing createTexture a format this device cannot sample is a hard
        // failure, so an unsupported bake is decoded back to RGBA8 and takes the path below
        // exactly as an unbaked texture does.
        if (asset != nullptr && asset->gpuBlocks.has_value()) {
            const bool deviceSupports = RenderDeviceSupportsTextureFormat(asset->gpuBlocks->format, colorSpace);
            if (SelectRenderTextureUploadPath(*asset, deviceSupports) == RenderTextureUploadPath::DecodedRgba8) {
                std::optional<RenderTextureAssetData> decoded = DecodeRenderTextureToRgba8(*asset);
                asset = decoded.has_value()
                    ? std::make_shared<const RenderTextureAssetData>(std::move(*decoded))
                    : nullptr;
            }
        }

        const TextureRef textureRef{ assetId, asset };
        const std::vector<std::uint8_t>* uploadBytes = textureRef.IsLoaded() && textureRef->gpuBlocks.has_value()
            ? &textureRef->gpuBlocks->blocks
            : (textureRef.IsLoaded() ? &textureRef->rgba8 : nullptr);
        if (uploadBytes == nullptr || uploadBytes->empty() ||
            uploadBytes->size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            context.sceneRenderer.ResourceMap().UnbindTexture(textureAssetId, colorSpace);
            return;
        }

        std::optional<RuntimeTextureMipChain> generatedMipChain;
        if (!textureRef->gpuBlocks.has_value() && textureRef->dimension == RenderTextureDimension::Texture2D &&
            textureRef->mipCount == 1U) {
            generatedMipChain = BuildRuntimeTexture2DMipChain(textureRef->rgba8, textureRef->width, textureRef->height, colorSpace);
            if (!generatedMipChain.has_value() ||
                generatedMipChain->rgba8.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                context.sceneRenderer.ResourceMap().UnbindTexture(textureAssetId, colorSpace);
                return;
            }
            uploadBytes = &generatedMipChain->rgba8;
        }

        const bgfx::Memory* memory = bgfx::copy(uploadBytes->data(), static_cast<std::uint32_t>(uploadBytes->size()));
        RenderTextureDesc textureDesc = textureRef->MakeDesc(memory, colorSpace);
        if (generatedMipChain.has_value()) {
            textureDesc.mipCount = generatedMipChain->mipCount;
        }
        const RenderTextureHandle handle = context.sceneRenderer.Resources().RegisterTexture(textureDesc);
        if (!handle.IsValid()) {
            context.sceneRenderer.ResourceMap().UnbindTexture(textureAssetId, colorSpace);
            static_cast<void>(manager.Unload(assetId));
            return;
        }

        textures[runtimeKey] = RuntimeTextureResource{
            .handle = handle,
            .contentHash = metadata->contentHash,
            .lastReferencedFrame = context.currentFrame,
            .dimension = textureRef->dimension,
        };
        context.sceneRenderer.ResourceMap().BindTexture(textureAssetId, colorSpace, handle);
        if (textureRef->dimension != expectedDimension) {
            std::ostringstream row;
            row << "texture-dimension-mismatch assetId=" << textureAssetId
                << " expected=" << RenderTextureDimensionName(expectedDimension)
                << " actual=" << RenderTextureDimensionName(textureRef->dimension);
            WriteRendererMaterialGraphDebugLog("resource", row.str());
        }
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
        for (const RenderMaterialTextureSlotBinding slot : RenderMaterialTextureSlots(*material)) {
            if (slot.policy.runtimeSupport == RenderMaterialFeatureSupport::Supported) {
                ensureTexture(
                    slot.assetId,
                    RenderTextureBindingColorSpace(slot.policy.expectedColorSpace),
                    RenderTextureDimension::Texture2D);
            }
        }
        for (const RenderMaterialGraphTextureBinding& graphTexture : material->graphProgram.textures) {
            ensureTexture(graphTexture.textureAssetId, graphTexture.colorSpace, graphTexture.dimension);
        }
    }
    // Some renderer features, such as the World Backdrop environment map, consume a
    // texture without an intervening material. They still go through the same cache,
    // validation and lifetime path as material textures.
    for (const RuntimeTextureAssetKey textureKey : context.frameReferences.Textures()) {
        if (textureKey.sceneId == context.scene.Id()) {
            ensureTexture(textureKey.assetId, textureKey.colorSpace, RenderTextureDimension::Texture2D);
        }
    }
}

} // namespace kb::render
