#pragma once

#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "kb/render/resources/RenderResources.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::render {

enum class RenderMaterialTextureSlotKind : std::uint8_t {
    Albedo,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
    Clearcoat,
    ClearcoatRoughness,
    SheenColor,
    Transmission,
    Thickness,
    Anisotropy,
    Decal,
    LayerMask,
};

struct RenderMaterialTextureSlotPolicy {
    RenderMaterialTextureSlotKind kind = RenderMaterialTextureSlotKind::Albedo;
    std::string_view assetIdFieldName;
    RenderMaterialTextureColorSpace expectedColorSpace = RenderMaterialTextureColorSpace::Unknown;
    RenderMaterialFeatureSupport runtimeSupport = RenderMaterialFeatureSupport::Supported;
};

struct RenderMaterialTextureSlotBinding {
    RenderMaterialTextureSlotPolicy policy{};
    RenderTextureHandle directHandle{};
    std::uint64_t assetId = 0;
};

inline constexpr std::array<RenderMaterialTextureSlotPolicy, 13U> kRenderMaterialTextureSlotPolicies{{
    { .kind = RenderMaterialTextureSlotKind::Albedo, .assetIdFieldName = "albedoTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Srgb, .runtimeSupport = RenderMaterialFeatureSupport::Supported },
    { .kind = RenderMaterialTextureSlotKind::Normal, .assetIdFieldName = "normalTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::Supported },
    { .kind = RenderMaterialTextureSlotKind::MetallicRoughness, .assetIdFieldName = "metallicRoughnessTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::Supported },
    { .kind = RenderMaterialTextureSlotKind::Occlusion, .assetIdFieldName = "occlusionTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::Supported },
    { .kind = RenderMaterialTextureSlotKind::Emissive, .assetIdFieldName = "emissiveTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Srgb, .runtimeSupport = RenderMaterialFeatureSupport::Supported },
    { .kind = RenderMaterialTextureSlotKind::Clearcoat, .assetIdFieldName = "clearcoatTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored },
    { .kind = RenderMaterialTextureSlotKind::ClearcoatRoughness, .assetIdFieldName = "clearcoatRoughnessTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored },
    { .kind = RenderMaterialTextureSlotKind::SheenColor, .assetIdFieldName = "sheenColorTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Srgb, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored },
    { .kind = RenderMaterialTextureSlotKind::Transmission, .assetIdFieldName = "transmissionTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored },
    { .kind = RenderMaterialTextureSlotKind::Thickness, .assetIdFieldName = "thicknessTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored },
    { .kind = RenderMaterialTextureSlotKind::Anisotropy, .assetIdFieldName = "anisotropyTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Linear, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored },
    { .kind = RenderMaterialTextureSlotKind::Decal, .assetIdFieldName = "decalTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Unknown, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored },
    { .kind = RenderMaterialTextureSlotKind::LayerMask, .assetIdFieldName = "layerMaskTextureAssetId", .expectedColorSpace = RenderMaterialTextureColorSpace::Unknown, .runtimeSupport = RenderMaterialFeatureSupport::ParsedButIgnored },
}};

[[nodiscard]] inline RenderTextureColorSpace RenderTextureBindingColorSpace(RenderMaterialTextureColorSpace expectedColorSpace) noexcept {
    return expectedColorSpace == RenderMaterialTextureColorSpace::Srgb ? RenderTextureColorSpace::Srgb : RenderTextureColorSpace::Linear;
}

[[nodiscard]] inline RenderMaterialTextureSlotBinding RenderMaterialTextureSlot(
    const RenderMaterialResource& material,
    RenderMaterialTextureSlotPolicy policy) noexcept {
    switch (policy.kind) {
    case RenderMaterialTextureSlotKind::Albedo:
        return { .policy = policy, .directHandle = material.albedoTexture, .assetId = material.albedoTextureAssetId };
    case RenderMaterialTextureSlotKind::Normal:
        return { .policy = policy, .directHandle = material.normalTexture, .assetId = material.normalTextureAssetId };
    case RenderMaterialTextureSlotKind::MetallicRoughness:
        return { .policy = policy, .directHandle = material.metallicRoughnessTexture, .assetId = material.metallicRoughnessTextureAssetId };
    case RenderMaterialTextureSlotKind::Occlusion:
        return { .policy = policy, .directHandle = material.occlusionTexture, .assetId = material.occlusionTextureAssetId };
    case RenderMaterialTextureSlotKind::Emissive:
        return { .policy = policy, .directHandle = material.emissiveTexture, .assetId = material.emissiveTextureAssetId };
    case RenderMaterialTextureSlotKind::Clearcoat:
        return { .policy = policy, .directHandle = material.clearcoatTexture, .assetId = material.clearcoatTextureAssetId };
    case RenderMaterialTextureSlotKind::ClearcoatRoughness:
        return { .policy = policy, .directHandle = material.clearcoatRoughnessTexture, .assetId = material.clearcoatRoughnessTextureAssetId };
    case RenderMaterialTextureSlotKind::SheenColor:
        return { .policy = policy, .directHandle = material.sheenColorTexture, .assetId = material.sheenColorTextureAssetId };
    case RenderMaterialTextureSlotKind::Transmission:
        return { .policy = policy, .directHandle = material.transmissionTexture, .assetId = material.transmissionTextureAssetId };
    case RenderMaterialTextureSlotKind::Thickness:
        return { .policy = policy, .directHandle = material.thicknessTexture, .assetId = material.thicknessTextureAssetId };
    case RenderMaterialTextureSlotKind::Anisotropy:
        return { .policy = policy, .directHandle = material.anisotropyTexture, .assetId = material.anisotropyTextureAssetId };
    case RenderMaterialTextureSlotKind::Decal:
        return { .policy = policy, .directHandle = material.decalTexture, .assetId = material.decalTextureAssetId };
    case RenderMaterialTextureSlotKind::LayerMask:
        return { .policy = policy, .directHandle = material.layerMaskTexture, .assetId = material.layerMaskTextureAssetId };
    }
    return {};
}

[[nodiscard]] inline RenderMaterialTextureSlotBinding RenderMaterialTextureSlot(
    const RenderMaterialResource& material,
    RenderMaterialTextureSlotKind kind) noexcept {
    for (const RenderMaterialTextureSlotPolicy policy : kRenderMaterialTextureSlotPolicies) {
        if (policy.kind == kind) {
            return RenderMaterialTextureSlot(material, policy);
        }
    }
    return {};
}

[[nodiscard]] inline std::array<RenderMaterialTextureSlotBinding, kRenderMaterialTextureSlotPolicies.size()> RenderMaterialTextureSlots(
    const RenderMaterialResource& material) noexcept {
    std::array<RenderMaterialTextureSlotBinding, kRenderMaterialTextureSlotPolicies.size()> slots{};
    for (std::size_t slotIndex = 0U; slotIndex < kRenderMaterialTextureSlotPolicies.size(); ++slotIndex) {
        slots[slotIndex] = RenderMaterialTextureSlot(material, kRenderMaterialTextureSlotPolicies[slotIndex]);
    }
    return slots;
}

} // namespace kb::render
