#include "SceneMeshMaterialBindingResolver.hpp"

#include "kb/render/resources/RenderMaterialTextureSlots.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <algorithm>
#include <sstream>
#include <string>

namespace kb::render {
namespace {

[[nodiscard]] bgfx::TextureHandle ResolveMaterialTexture(
    const RenderMaterialTextureSlotBinding& slot,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    RenderTextureHandle textureHandle = slot.directHandle;
    if (!textureHandle.IsValid() && slot.assetId != 0U) {
        textureHandle = resourceMap.ResolveTexture(slot.assetId, RenderTextureBindingColorSpace(slot.policy.expectedColorSpace));
    }

    const RenderTextureResource* texture = resources.FindTexture(textureHandle);
    const bool resolved = texture != nullptr && bgfx::isValid(texture->texture);
    {
        std::ostringstream row;
        row << "slot-resolve assetId=" << slot.assetId
            << " directHandle=" << slot.directHandle.value
            << " resolvedHandle=" << textureHandle.value
            << " expectedColorSpace=" << static_cast<int>(slot.policy.expectedColorSpace)
            << " foundResource=" << (texture != nullptr ? "true" : "false")
            << " resolved=" << (resolved ? "true" : "false")
            << " bgfxHandle=" << (resolved ? std::to_string(texture->texture.idx) : std::string{ "fallback" })
            << " fallbackHandle=" << (bgfx::isValid(fallback) ? std::to_string(fallback.idx) : std::string{ "invalid" });
        if (texture != nullptr) {
            row << " size=" << texture->width << 'x' << texture->height
                << " format=" << static_cast<int>(texture->format)
                << " colorSpace=" << static_cast<int>(texture->colorSpace)
                << " version=" << texture->version;
        }
        WriteRendererMaterialGraphDebugLog("resolver", row.str());
    }
    return resolved ? texture->texture : fallback;
}

[[nodiscard]] bool HasResolvedMaterialTexture(
    const RenderMaterialTextureSlotBinding& slot,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap) noexcept {
    RenderTextureHandle textureHandle = slot.directHandle;
    if (!textureHandle.IsValid() && slot.assetId != 0U) {
        textureHandle = resourceMap.ResolveTexture(slot.assetId, RenderTextureBindingColorSpace(slot.policy.expectedColorSpace));
    }

    const RenderTextureResource* texture = resources.FindTexture(textureHandle);
    const bool resolved = texture != nullptr && bgfx::isValid(texture->texture);
    {
        std::ostringstream row;
        row << "slot-probe assetId=" << slot.assetId
            << " directHandle=" << slot.directHandle.value
            << " resolvedHandle=" << textureHandle.value
            << " expectedColorSpace=" << static_cast<int>(slot.policy.expectedColorSpace)
            << " foundResource=" << (texture != nullptr ? "true" : "false")
            << " resolved=" << (resolved ? "true" : "false");
        WriteRendererMaterialGraphDebugLog("resolver", row.str());
    }
    return resolved;
}

[[nodiscard]] bgfx::TextureHandle ResolveAlbedoTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(RenderMaterialTextureSlot(*material, RenderMaterialTextureSlotKind::Albedo), resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveNormalTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(RenderMaterialTextureSlot(*material, RenderMaterialTextureSlotKind::Normal), resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveMetallicRoughnessTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(RenderMaterialTextureSlot(*material, RenderMaterialTextureSlotKind::MetallicRoughness), resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveEmissiveTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(RenderMaterialTextureSlot(*material, RenderMaterialTextureSlotKind::Emissive), resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveOcclusionTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(RenderMaterialTextureSlot(*material, RenderMaterialTextureSlotKind::Occlusion), resources, resourceMap, fallback);
}

[[nodiscard]] std::array<float, 4> MaterialParams(const RenderMaterialResource* material, bool normalTextureResolved) noexcept {
    if (material == nullptr) {
        return { 0.0F, 1.0F, 0.0F, 0.5F };
    }
    return {
        std::clamp(material->metallicFactor, 0.0F, 1.0F),
        std::clamp(material->roughnessFactor, 0.04F, 1.0F),
        normalTextureResolved ? material->normalScale : 0.0F,
        material->alphaCutoff,
    };
}

[[nodiscard]] std::array<float, 4> MaterialEmissive(const RenderMaterialResource* material) noexcept {
    if (material == nullptr) {
        return { 0.0F, 0.0F, 0.0F, 1.0F };
    }
    return {
        std::max(material->emissiveColor[0], 0.0F),
        std::max(material->emissiveColor[1], 0.0F),
        std::max(material->emissiveColor[2], 0.0F),
        std::max(material->emissiveStrength, 0.0F),
    };
}

[[nodiscard]] float MaterialAlphaModeValue(RenderMaterialAlphaMode mode) noexcept {
    switch (mode) {
    case RenderMaterialAlphaMode::Opaque:
        return 0.0F;
    case RenderMaterialAlphaMode::Mask:
        return 1.0F;
    case RenderMaterialAlphaMode::Blend:
        return 2.0F;
    }
    return 0.0F;
}

[[nodiscard]] std::array<float, 4> MaterialFlags(const RenderMaterialResource* material) noexcept {
    return {
        MaterialAlphaModeValue(material == nullptr ? RenderMaterialAlphaMode::Opaque : material->alphaMode),
        std::clamp(material == nullptr ? 1.0F : material->occlusionStrength, 0.0F, 1.0F),
        0.0F,
        0.0F,
    };
}

[[nodiscard]] std::array<float, 4> MaterialUvTransform(const RenderMaterialResource* material) noexcept {
    if (material == nullptr) {
        return { 1.0F, 1.0F, 0.0F, 0.0F };
    }
    return {
        material->uvTiling[0],
        material->uvTiling[1],
        material->uvOffset[0],
        material->uvOffset[1],
    };
}

} // namespace

SceneMeshMaterialBinding SceneMeshMaterialBindingResolver::Resolve(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    SceneMeshMaterialBindingFallbacks fallbacks) noexcept {
    const bool normalTextureResolved = material != nullptr && HasResolvedMaterialTexture(RenderMaterialTextureSlot(*material, RenderMaterialTextureSlotKind::Normal), resources, resourceMap);
    return SceneMeshMaterialBinding{
        .albedoTexture = ResolveAlbedoTexture(material, resources, resourceMap, fallbacks.whiteTexture),
        .normalTexture = ResolveNormalTexture(material, resources, resourceMap, fallbacks.normalTexture),
        .metallicRoughnessTexture = ResolveMetallicRoughnessTexture(material, resources, resourceMap, fallbacks.whiteTexture),
        .occlusionTexture = ResolveOcclusionTexture(material, resources, resourceMap, fallbacks.whiteTexture),
        .emissiveTexture = ResolveEmissiveTexture(material, resources, resourceMap, fallbacks.whiteTexture),
        .params = MaterialParams(material, normalTextureResolved),
        .emissive = MaterialEmissive(material),
        .flags = MaterialFlags(material),
        .uvTransform = MaterialUvTransform(material),
    };
}

SceneMeshShadowMaterialBinding SceneMeshMaterialBindingResolver::ResolveShadow(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    SceneMeshMaterialBindingFallbacks fallbacks) noexcept {
    return SceneMeshShadowMaterialBinding{
        .albedoTexture = ResolveAlbedoTexture(material, resources, resourceMap, fallbacks.whiteTexture),
        .params = MaterialParams(material, false),
        .flags = MaterialFlags(material),
        .uvTransform = MaterialUvTransform(material),
    };
}

} // namespace kb::render
