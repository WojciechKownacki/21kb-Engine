#include "scene/cache/SceneMaterialTextureDependencySignature.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <array>
#include <cstddef>

namespace kb::render {
namespace {

struct MaterialTextureDependencySlot {
    RenderTextureHandle directHandle{};
    std::uint64_t assetId = 0;
    RenderTextureColorSpace colorSpace = RenderTextureColorSpace::Linear;
};

void HashCombine(std::uint64_t& seed, std::uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

[[nodiscard]] std::array<MaterialTextureDependencySlot, 13U> TextureSlots(const RenderMaterialResource& material) noexcept {
    return {{
        MaterialTextureDependencySlot{ .directHandle = material.albedoTexture, .assetId = material.albedoTextureAssetId, .colorSpace = RenderTextureColorSpace::Srgb },
        MaterialTextureDependencySlot{ .directHandle = material.normalTexture, .assetId = material.normalTextureAssetId, .colorSpace = RenderTextureColorSpace::Linear },
        MaterialTextureDependencySlot{ .directHandle = material.metallicRoughnessTexture, .assetId = material.metallicRoughnessTextureAssetId, .colorSpace = RenderTextureColorSpace::Linear },
        MaterialTextureDependencySlot{ .directHandle = material.occlusionTexture, .assetId = material.occlusionTextureAssetId, .colorSpace = RenderTextureColorSpace::Linear },
        MaterialTextureDependencySlot{ .directHandle = material.emissiveTexture, .assetId = material.emissiveTextureAssetId, .colorSpace = RenderTextureColorSpace::Srgb },
        MaterialTextureDependencySlot{ .directHandle = material.clearcoatTexture, .assetId = material.clearcoatTextureAssetId },
        MaterialTextureDependencySlot{ .directHandle = material.clearcoatRoughnessTexture, .assetId = material.clearcoatRoughnessTextureAssetId },
        MaterialTextureDependencySlot{ .directHandle = material.sheenColorTexture, .assetId = material.sheenColorTextureAssetId },
        MaterialTextureDependencySlot{ .directHandle = material.transmissionTexture, .assetId = material.transmissionTextureAssetId },
        MaterialTextureDependencySlot{ .directHandle = material.thicknessTexture, .assetId = material.thicknessTextureAssetId },
        MaterialTextureDependencySlot{ .directHandle = material.anisotropyTexture, .assetId = material.anisotropyTextureAssetId },
        MaterialTextureDependencySlot{ .directHandle = material.decalTexture, .assetId = material.decalTextureAssetId },
        MaterialTextureDependencySlot{ .directHandle = material.layerMaskTexture, .assetId = material.layerMaskTextureAssetId },
    }};
}

[[nodiscard]] RenderTextureHandle ResolveTextureHandle(
    const MaterialTextureDependencySlot& slot,
    const SceneRenderResourceMap* resourceMap) noexcept {
    if (slot.directHandle.IsValid()) {
        return slot.directHandle;
    }
    return resourceMap == nullptr || slot.assetId == 0U ? RenderTextureHandle{} : resourceMap->ResolveTexture(slot.assetId, slot.colorSpace);
}

[[nodiscard]] const RenderTextureResource* ResolveTextureResource(
    RenderTextureHandle handle,
    const RenderResourceRegistry* resources) noexcept {
    return resources == nullptr || !handle.IsValid() ? nullptr : resources->FindTexture(handle);
}

} // namespace

std::uint64_t SceneMaterialTextureDependencySignature::Build(const SceneMaterialTextureDependencyDesc& desc) noexcept {
    if (desc.material == nullptr) {
        return 0U;
    }

    std::uint64_t signature = 0xcbf29ce484222325ULL;
    const std::array<MaterialTextureDependencySlot, 13U> slots = TextureSlots(*desc.material);
    for (std::size_t slotIndex = 0U; slotIndex < slots.size(); ++slotIndex) {
        const MaterialTextureDependencySlot& slot = slots[slotIndex];
        const RenderTextureHandle resolvedHandle = ResolveTextureHandle(slot, desc.resourceMap);
        const RenderTextureResource* texture = ResolveTextureResource(resolvedHandle, desc.resources);

        HashCombine(signature, static_cast<std::uint64_t>(slotIndex + 1U));
        HashCombine(signature, slot.assetId);
        HashCombine(signature, static_cast<std::uint64_t>(slot.colorSpace));
        HashCombine(signature, slot.directHandle.value);
        HashCombine(signature, resolvedHandle.value);
        HashCombine(signature, texture == nullptr ? 0U : texture->version);
    }
    return signature;
}

} // namespace kb::render
