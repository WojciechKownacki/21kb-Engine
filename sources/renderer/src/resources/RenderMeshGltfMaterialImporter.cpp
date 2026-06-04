#include "resources/RenderMeshGltfMaterialImporter.hpp"

#include <algorithm>
#include <iterator>
#include <string>

namespace kb::render {
namespace {

[[nodiscard]] std::uint64_t MaterialAssetIdForName(std::string_view materialName, const RenderMeshGltfImportDesc& desc) noexcept {
    for (std::uint32_t bindingIndex = 0U; bindingIndex < desc.materialBindingCount; ++bindingIndex) {
        const RenderMeshAssetMaterialBinding& binding = desc.materialBindings[bindingIndex];
        if (binding.materialName == materialName) {
            return binding.materialAssetId;
        }
    }
    return 0U;
}

[[nodiscard]] RenderMaterialAlphaMode AlphaModeOf(cgltf_alpha_mode mode) noexcept {
    switch (mode) {
    case cgltf_alpha_mode_mask:
        return RenderMaterialAlphaMode::Mask;
    case cgltf_alpha_mode_blend:
        return RenderMaterialAlphaMode::Blend;
    case cgltf_alpha_mode_opaque:
    case cgltf_alpha_mode_max_enum:
        return RenderMaterialAlphaMode::Opaque;
    }
    return RenderMaterialAlphaMode::Opaque;
}

[[nodiscard]] std::string TextureUriOf(const cgltf_texture_view& textureView) {
    if (textureView.texture == nullptr || textureView.texture->image == nullptr || textureView.texture->image->uri == nullptr) {
        return {};
    }
    const std::string_view uri{ textureView.texture->image->uri };
    if (uri.starts_with("data:") || uri.find("://") != std::string_view::npos) {
        return {};
    }
    return std::string{ uri };
}

[[nodiscard]] RenderMeshEmbeddedMaterial BuildEmbeddedMaterial(std::string_view materialName, const cgltf_material* material) {
    RenderMeshEmbeddedMaterial embedded{};
    embedded.name = std::string{ materialName };
    if (material == nullptr) {
        return embedded;
    }

    for (std::uint32_t channel = 0U; channel < 4U; ++channel) {
        embedded.desc.baseColor[channel] = material->pbr_metallic_roughness.base_color_factor[channel];
    }
    for (std::uint32_t channel = 0U; channel < 3U; ++channel) {
        embedded.desc.emissiveColor[channel] = material->emissive_factor[channel];
    }
    embedded.desc.metallicFactor = material->pbr_metallic_roughness.metallic_factor;
    embedded.desc.roughnessFactor = material->pbr_metallic_roughness.roughness_factor;
    embedded.desc.normalScale = material->normal_texture.texture == nullptr ? 1.0F : material->normal_texture.scale;
    embedded.desc.occlusionStrength = material->occlusion_texture.texture == nullptr ? 1.0F : material->occlusion_texture.scale;
    embedded.desc.emissiveStrength = material->has_emissive_strength ? material->emissive_strength.emissive_strength : 1.0F;
    embedded.desc.alphaCutoff = material->alpha_cutoff;
    embedded.desc.alphaMode = AlphaModeOf(material->alpha_mode);
    embedded.desc.doubleSided = material->double_sided;
    embedded.albedoTexturePath = TextureUriOf(material->pbr_metallic_roughness.base_color_texture);
    embedded.normalTexturePath = TextureUriOf(material->normal_texture);
    embedded.metallicRoughnessTexturePath = TextureUriOf(material->pbr_metallic_roughness.metallic_roughness_texture);
    embedded.occlusionTexturePath = TextureUriOf(material->occlusion_texture);
    embedded.emissiveTexturePath = TextureUriOf(material->emissive_texture);
    return embedded;
}

} // namespace

std::uint32_t RenderMeshGltfMaterialImporter::EnsureMaterialSlot(
    RenderMeshAssetData& asset,
    std::string_view materialName,
    const cgltf_material* material,
    const RenderMeshGltfImportDesc& desc) {
    const auto iterator = std::ranges::find_if(asset.materialNames, [materialName](const std::string& name) {
        return name == materialName;
    });
    if (iterator != asset.materialNames.end()) {
        return static_cast<std::uint32_t>(std::distance(asset.materialNames.begin(), iterator));
    }

    asset.materialNames.push_back(std::string{ materialName });
    asset.embeddedMaterials.push_back(BuildEmbeddedMaterial(materialName, material));
    asset.materialSlots.push_back(RenderMaterialSlotDesc{
        .defaultMaterialAssetId = MaterialAssetIdForName(materialName, desc),
    });
    return static_cast<std::uint32_t>(asset.materialSlots.size() - 1U);
}

} // namespace kb::render
