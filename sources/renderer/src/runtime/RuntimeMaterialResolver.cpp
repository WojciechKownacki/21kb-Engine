#include "kb/render/runtime/RuntimeMaterialResolver.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"

#include <filesystem>
#include <string>

namespace kb::render {

std::uint64_t RuntimeMaterialResolver::EmbeddedMaterialAssetId(std::uint64_t meshAssetId, std::uint32_t slotIndex, std::string_view materialName) noexcept {
    std::string key = "RenderMeshEmbeddedMaterial:";
    key += std::to_string(meshAssetId);
    key += ':';
    key += std::to_string(slotIndex);
    key += ':';
    key += materialName;
    return kb::assets::MakeAssetId(key).value;
}

ResolvedRuntimeMaterialDesc RuntimeMaterialResolver::ResolveEmbeddedMaterial(
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& meshMetadata,
    const RenderMeshEmbeddedMaterial& embeddedMaterial) const {
    ResolvedRuntimeMaterialDesc resolved{};
    resolved.desc = embeddedMaterial.desc;
    resolved.desc.albedoTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.albedoTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.normalTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.normalTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.metallicRoughnessTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.metallicRoughnessTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.occlusionTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.occlusionTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.emissiveTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.emissiveTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.clearcoatTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.clearcoatTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.clearcoatRoughnessTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.clearcoatRoughnessTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.sheenColorTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.sheenColorTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.transmissionTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.transmissionTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.thicknessTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.thicknessTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.anisotropyTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.anisotropyTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.decalTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.decalTexturePath, resolved.unresolvedTexturePathCount);
    resolved.desc.layerMaskTextureAssetId = ResolveTextureAssetIdOrCount(manager, meshMetadata, embeddedMaterial.layerMaskTexturePath, resolved.unresolvedTexturePathCount);
    return resolved;
}

ResolvedRuntimeMaterialDesc RuntimeMaterialResolver::ResolveLoadedMaterial(
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& materialMetadata,
    const RenderMaterialAssetData& materialAsset) const {
    ResolvedRuntimeMaterialDesc resolved{};
    resolved.desc = materialAsset.desc;
    if (!materialAsset.albedoTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.albedoTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.albedoTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.albedoTextureAssetId;
    }
    if (!materialAsset.normalTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.normalTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.normalTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.normalTextureAssetId;
    }
    if (!materialAsset.metallicRoughnessTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.metallicRoughnessTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.metallicRoughnessTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.metallicRoughnessTextureAssetId;
    }
    if (!materialAsset.occlusionTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.occlusionTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.occlusionTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.occlusionTextureAssetId;
    }
    if (!materialAsset.emissiveTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.emissiveTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.emissiveTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.emissiveTextureAssetId;
    }
    if (!materialAsset.clearcoatTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.clearcoatTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.clearcoatTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.clearcoatTextureAssetId;
    }
    if (!materialAsset.clearcoatRoughnessTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.clearcoatRoughnessTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.clearcoatRoughnessTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.clearcoatRoughnessTextureAssetId;
    }
    if (!materialAsset.sheenColorTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.sheenColorTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.sheenColorTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.sheenColorTextureAssetId;
    }
    if (!materialAsset.transmissionTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.transmissionTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.transmissionTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.transmissionTextureAssetId;
    }
    if (!materialAsset.thicknessTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.thicknessTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.thicknessTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.thicknessTextureAssetId;
    }
    if (!materialAsset.anisotropyTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.anisotropyTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.anisotropyTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.anisotropyTextureAssetId;
    }
    if (!materialAsset.decalTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.decalTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.decalTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.decalTextureAssetId;
    }
    if (!materialAsset.layerMaskTexturePath.empty()) {
        const std::uint64_t textureAssetId = ResolveTextureAssetIdOrCount(manager, materialMetadata, materialAsset.layerMaskTexturePath, resolved.unresolvedTexturePathCount);
        resolved.desc.layerMaskTextureAssetId = textureAssetId != 0U ? textureAssetId : resolved.desc.layerMaskTextureAssetId;
    }
    return resolved;
}

std::uint64_t RuntimeMaterialResolver::ResolveTextureAssetId(
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& ownerMetadata,
    std::string_view texturePath) const {
    if (texturePath.empty()) {
        return 0U;
    }

    const std::filesystem::path textureVirtualPath{ std::string{ texturePath } };
    const std::filesystem::path candidate = textureVirtualPath.is_absolute()
        ? textureVirtualPath
        : (ownerMetadata.virtualPath.parent_path() / textureVirtualPath).lexically_normal();
    const kb::assets::AssetMetadata* textureMetadata = manager.Registry().FindByPath(candidate);
    if (textureMetadata == nullptr || textureMetadata->type != "RenderTexture") {
        return 0U;
    }
    return textureMetadata->id.value;
}

std::uint64_t RuntimeMaterialResolver::ResolveTextureAssetIdOrCount(
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& ownerMetadata,
    std::string_view texturePath,
    std::uint32_t& unresolvedTexturePathCount) const {
    const std::uint64_t textureAssetId = ResolveTextureAssetId(manager, ownerMetadata, texturePath);
    if (!texturePath.empty() && textureAssetId == 0U) {
        ++unresolvedTexturePathCount;
    }
    return textureAssetId;
}

} // namespace kb::render
