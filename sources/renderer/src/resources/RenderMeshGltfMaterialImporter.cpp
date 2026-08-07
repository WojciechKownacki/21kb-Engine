#include "resources/RenderMeshGltfMaterialImporter.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <optional>
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

[[nodiscard]] std::optional<char> DecodeHexByte(char high, char low) noexcept {
    const auto hexValue = [](char value) -> std::optional<unsigned char> {
        if (value >= '0' && value <= '9') {
            return static_cast<unsigned char>(value - '0');
        }
        if (value >= 'A' && value <= 'F') {
            return static_cast<unsigned char>(value - 'A' + 10);
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<unsigned char>(value - 'a' + 10);
        }
        return std::nullopt;
    };

    const std::optional<unsigned char> upper = hexValue(high);
    const std::optional<unsigned char> lower = hexValue(low);
    if (!upper.has_value() || !lower.has_value()) {
        return std::nullopt;
    }
    return static_cast<char>((*upper << 4U) | *lower);
}

[[nodiscard]] std::optional<std::string> PercentDecodeUriPath(std::string_view uri) {
    std::string decoded;
    decoded.reserve(uri.size());
    for (std::size_t index = 0U; index < uri.size(); ++index) {
        if (uri[index] != '%') {
            decoded.push_back(uri[index]);
            continue;
        }
        if (index + 2U >= uri.size()) {
            return std::nullopt;
        }
        const std::optional<char> value = DecodeHexByte(uri[index + 1U], uri[index + 2U]);
        if (!value.has_value()) {
            return std::nullopt;
        }
        decoded.push_back(*value);
        index += 2U;
    }
    return decoded;
}

[[nodiscard]] bool IsSafeRelativeTextureUriPath(std::string_view path) {
    if (path.empty() || path.find('\\') != std::string_view::npos) {
        return false;
    }

    const std::filesystem::path texturePath{ std::string{ path } };
    if (texturePath.is_absolute() || texturePath.has_root_name()) {
        return false;
    }
    for (const std::filesystem::path& part : texturePath) {
        if (part == "." || part == ".." || part.empty()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string TextureUriOf(const cgltf_texture_view& textureView) {
    if (textureView.texture == nullptr || textureView.texture->image == nullptr || textureView.texture->image->uri == nullptr) {
        return {};
    }
    const std::string_view uri{ textureView.texture->image->uri };
    if (uri.starts_with("data:") || uri.find("://") != std::string_view::npos) {
        return {};
    }
    const std::optional<std::string> decoded = PercentDecodeUriPath(uri);
    if (!decoded.has_value() || !IsSafeRelativeTextureUriPath(*decoded)) {
        return {};
    }
    return *decoded;
}

[[nodiscard]] bool IsRepresentableMaterialUvTransform(const cgltf_texture_view& view) noexcept {
    constexpr float kRotationEpsilon = 0.000001F;
    return view.texture != nullptr &&
        view.has_transform &&
        (!view.transform.has_texcoord || view.transform.texcoord == 0) &&
        std::abs(view.transform.rotation) <= kRotationEpsilon;
}

[[nodiscard]] const cgltf_texture_view* MaterialUvTransformView(const cgltf_material& material) noexcept {
    const cgltf_texture_view* views[]{
        &material.pbr_metallic_roughness.base_color_texture,
        &material.pbr_metallic_roughness.metallic_roughness_texture,
        &material.normal_texture,
        &material.occlusion_texture,
        &material.emissive_texture,
    };
    for (const cgltf_texture_view* view : views) {
        if (view != nullptr && IsRepresentableMaterialUvTransform(*view)) {
            return view;
        }
    }
    return nullptr;
}

void ApplyMaterialUvTransform(RenderMaterialDesc& desc, const cgltf_material& material) noexcept {
    const cgltf_texture_view* transformView = MaterialUvTransformView(material);
    if (transformView == nullptr) {
        return;
    }

    desc.uvTiling[0] = transformView->transform.scale[0];
    desc.uvTiling[1] = transformView->transform.scale[1];
    desc.uvOffset[0] = transformView->transform.offset[0];
    desc.uvOffset[1] = transformView->transform.offset[1];
}

[[nodiscard]] RenderMeshEmbeddedMaterial BuildEmbeddedMaterialImpl(std::string_view materialName, const cgltf_material* material) {
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
    ApplyMaterialUvTransform(embedded.desc, *material);
    embedded.albedoTexturePath = TextureUriOf(material->pbr_metallic_roughness.base_color_texture);
    embedded.normalTexturePath = TextureUriOf(material->normal_texture);
    embedded.metallicRoughnessTexturePath = TextureUriOf(material->pbr_metallic_roughness.metallic_roughness_texture);
    embedded.occlusionTexturePath = TextureUriOf(material->occlusion_texture);
    embedded.emissiveTexturePath = TextureUriOf(material->emissive_texture);
    return embedded;
}

} // namespace

RenderMeshEmbeddedMaterial RenderMeshGltfMaterialImporter::BuildEmbeddedMaterial(
    std::string_view materialName,
    const cgltf_material* material) {
    return BuildEmbeddedMaterialImpl(materialName, material);
}

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
    asset.embeddedMaterials.push_back(BuildEmbeddedMaterialImpl(materialName, material));
    asset.materialSlots.push_back(RenderMaterialSlotDesc{
        .defaultMaterialAssetId = MaterialAssetIdForName(materialName, desc),
    });
    return static_cast<std::uint32_t>(asset.materialSlots.size() - 1U);
}

} // namespace kb::render
