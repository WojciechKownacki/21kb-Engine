#include "scene/cache/SceneMaterialTextureDependencySignature.hpp"

#include "kb/render/resources/RenderMaterialTextureSlots.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <cstddef>

namespace kb::render {
namespace {

void HashCombine(std::uint64_t& seed, std::uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

[[nodiscard]] RenderTextureHandle ResolveTextureHandle(
    const RenderMaterialTextureSlotBinding& slot,
    const SceneRenderResourceMap* resourceMap) noexcept {
    if (slot.directHandle.IsValid()) {
        return slot.directHandle;
    }
    return resourceMap == nullptr || slot.assetId == 0U
        ? RenderTextureHandle{}
        : resourceMap->ResolveTexture(slot.assetId, RenderTextureBindingColorSpace(slot.policy.expectedColorSpace));
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
    const std::array<RenderMaterialTextureSlotBinding, kRenderMaterialTextureSlotPolicies.size()> slots = RenderMaterialTextureSlots(*desc.material);
    for (std::size_t slotIndex = 0U; slotIndex < slots.size(); ++slotIndex) {
        const RenderMaterialTextureSlotBinding& slot = slots[slotIndex];
        const RenderTextureHandle resolvedHandle = ResolveTextureHandle(slot, desc.resourceMap);
        const RenderTextureResource* texture = ResolveTextureResource(resolvedHandle, desc.resources);

        HashCombine(signature, static_cast<std::uint64_t>(slotIndex + 1U));
        HashCombine(signature, slot.assetId);
        HashCombine(signature, static_cast<std::uint64_t>(slot.policy.expectedColorSpace));
        HashCombine(signature, slot.directHandle.value);
        HashCombine(signature, resolvedHandle.value);
        HashCombine(signature, texture == nullptr ? 0U : texture->version);
    }
    return signature;
}

} // namespace kb::render
