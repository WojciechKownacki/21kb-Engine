#include "SceneMeshMaterialBindingResolver.hpp"

#include <algorithm>

namespace kb::render {
namespace {

[[nodiscard]] bgfx::TextureHandle ResolveMaterialTexture(
    RenderTextureHandle directTexture,
    std::uint64_t textureAssetId,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    RenderTextureHandle textureHandle = directTexture;
    if (!textureHandle.IsValid() && textureAssetId != 0U) {
        textureHandle = resourceMap.ResolveTexture(textureAssetId);
    }

    const RenderTextureResource* texture = resources.FindTexture(textureHandle);
    return texture == nullptr || !bgfx::isValid(texture->texture) ? fallback : texture->texture;
}

[[nodiscard]] bool HasResolvedMaterialTexture(
    RenderTextureHandle directTexture,
    std::uint64_t textureAssetId,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap) noexcept {
    RenderTextureHandle textureHandle = directTexture;
    if (!textureHandle.IsValid() && textureAssetId != 0U) {
        textureHandle = resourceMap.ResolveTexture(textureAssetId);
    }

    const RenderTextureResource* texture = resources.FindTexture(textureHandle);
    return texture != nullptr && bgfx::isValid(texture->texture);
}

[[nodiscard]] bgfx::TextureHandle ResolveAlbedoTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(material->albedoTexture, material->albedoTextureAssetId, resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveNormalTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(material->normalTexture, material->normalTextureAssetId, resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveMetallicRoughnessTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(material->metallicRoughnessTexture, material->metallicRoughnessTextureAssetId, resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveEmissiveTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(material->emissiveTexture, material->emissiveTextureAssetId, resources, resourceMap, fallback);
}

[[nodiscard]] bgfx::TextureHandle ResolveOcclusionTexture(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    bgfx::TextureHandle fallback) noexcept {
    return material == nullptr
        ? fallback
        : ResolveMaterialTexture(material->occlusionTexture, material->occlusionTextureAssetId, resources, resourceMap, fallback);
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

} // namespace

SceneMeshMaterialBinding SceneMeshMaterialBindingResolver::Resolve(
    const RenderMaterialResource* material,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    SceneMeshMaterialBindingFallbacks fallbacks) noexcept {
    const bool normalTextureResolved = material != nullptr && HasResolvedMaterialTexture(material->normalTexture, material->normalTextureAssetId, resources, resourceMap);
    return SceneMeshMaterialBinding{
        .albedoTexture = ResolveAlbedoTexture(material, resources, resourceMap, fallbacks.whiteTexture),
        .normalTexture = ResolveNormalTexture(material, resources, resourceMap, fallbacks.normalTexture),
        .metallicRoughnessTexture = ResolveMetallicRoughnessTexture(material, resources, resourceMap, fallbacks.whiteTexture),
        .occlusionTexture = ResolveOcclusionTexture(material, resources, resourceMap, fallbacks.whiteTexture),
        .emissiveTexture = ResolveEmissiveTexture(material, resources, resourceMap, fallbacks.blackTexture),
        .params = MaterialParams(material, normalTextureResolved),
        .emissive = MaterialEmissive(material),
        .flags = MaterialFlags(material),
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
    };
}

} // namespace kb::render
