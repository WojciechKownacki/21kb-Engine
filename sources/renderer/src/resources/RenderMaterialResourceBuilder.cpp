#include "resources/RenderMaterialResourceBuilder.hpp"

#include <cstring>

namespace kb::render {

RenderMaterialResource RenderMaterialResourceBuilder::Build(const RenderMaterialDesc& desc) noexcept {
    RenderMaterialResource resource{};
    std::memcpy(resource.baseColor, desc.baseColor, sizeof(resource.baseColor));
    std::memcpy(resource.emissiveColor, desc.emissiveColor, sizeof(resource.emissiveColor));
    resource.metallicFactor = desc.metallicFactor;
    resource.roughnessFactor = desc.roughnessFactor;
    resource.normalScale = desc.normalScale;
    resource.occlusionStrength = desc.occlusionStrength;
    resource.emissiveStrength = desc.emissiveStrength;
    resource.alphaCutoff = desc.alphaCutoff;
    resource.clearcoatFactor = desc.clearcoatFactor;
    resource.clearcoatRoughnessFactor = desc.clearcoatRoughnessFactor;
    std::memcpy(resource.sheenColor, desc.sheenColor, sizeof(resource.sheenColor));
    resource.sheenRoughnessFactor = desc.sheenRoughnessFactor;
    resource.transmissionFactor = desc.transmissionFactor;
    resource.thicknessFactor = desc.thicknessFactor;
    std::memcpy(resource.attenuationColor, desc.attenuationColor, sizeof(resource.attenuationColor));
    resource.attenuationDistance = desc.attenuationDistance;
    std::memcpy(resource.subsurfaceColor, desc.subsurfaceColor, sizeof(resource.subsurfaceColor));
    resource.subsurfaceFactor = desc.subsurfaceFactor;
    resource.anisotropyStrength = desc.anisotropyStrength;
    resource.anisotropyRotation = desc.anisotropyRotation;
    resource.layerWeight = desc.layerWeight;
    resource.alphaMode = desc.alphaMode;
    resource.decalBlendMode = desc.decalBlendMode;
    resource.layerBlendMode = desc.layerBlendMode;
    resource.doubleSided = desc.doubleSided;
    resource.albedoTextureAssetId = desc.albedoTextureAssetId;
    resource.normalTextureAssetId = desc.normalTextureAssetId;
    resource.metallicRoughnessTextureAssetId = desc.metallicRoughnessTextureAssetId;
    resource.occlusionTextureAssetId = desc.occlusionTextureAssetId;
    resource.emissiveTextureAssetId = desc.emissiveTextureAssetId;
    resource.clearcoatTextureAssetId = desc.clearcoatTextureAssetId;
    resource.clearcoatRoughnessTextureAssetId = desc.clearcoatRoughnessTextureAssetId;
    resource.sheenColorTextureAssetId = desc.sheenColorTextureAssetId;
    resource.transmissionTextureAssetId = desc.transmissionTextureAssetId;
    resource.thicknessTextureAssetId = desc.thicknessTextureAssetId;
    resource.anisotropyTextureAssetId = desc.anisotropyTextureAssetId;
    resource.decalTextureAssetId = desc.decalTextureAssetId;
    resource.layerMaskTextureAssetId = desc.layerMaskTextureAssetId;
    resource.albedoTexture = desc.albedoTexture;
    resource.normalTexture = desc.normalTexture;
    resource.metallicRoughnessTexture = desc.metallicRoughnessTexture;
    resource.occlusionTexture = desc.occlusionTexture;
    resource.emissiveTexture = desc.emissiveTexture;
    resource.clearcoatTexture = desc.clearcoatTexture;
    resource.clearcoatRoughnessTexture = desc.clearcoatRoughnessTexture;
    resource.sheenColorTexture = desc.sheenColorTexture;
    resource.transmissionTexture = desc.transmissionTexture;
    resource.thicknessTexture = desc.thicknessTexture;
    resource.anisotropyTexture = desc.anisotropyTexture;
    resource.decalTexture = desc.decalTexture;
    resource.layerMaskTexture = desc.layerMaskTexture;
    return resource;
}

} // namespace kb::render
