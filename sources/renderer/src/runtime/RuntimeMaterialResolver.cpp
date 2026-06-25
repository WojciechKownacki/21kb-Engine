#include "kb/render/runtime/RuntimeMaterialResolver.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace kb::render {
namespace {

[[nodiscard]] std::uint64_t HashCombine(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return lhs ^ (rhs + 0x9e3779b97f4a7c15ULL + (lhs << 6U) + (lhs >> 2U));
}

[[nodiscard]] std::filesystem::path ResolveAssetPhysicalPath(const kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata) {
    if (!metadata.physicalPath.empty()) {
        return metadata.physicalPath;
    }
    return manager.Mounts().Resolve(metadata.virtualPath).value_or(std::filesystem::path{});
}

[[nodiscard]] RuntimeMaterialResolveDiagnosticSeverity ConvertSeverity(RenderMaterialAssetParseDiagnosticSeverity severity) noexcept {
    return severity == RenderMaterialAssetParseDiagnosticSeverity::Warning
        ? RuntimeMaterialResolveDiagnosticSeverity::Warning
        : RuntimeMaterialResolveDiagnosticSeverity::Error;
}

[[nodiscard]] std::string ParseDiagnosticMessage(const RenderMaterialAssetParseDiagnostic& diagnostic) {
    std::string message{ RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code) };
    if (diagnostic.line > 0U) {
        message += " line ";
        message += std::to_string(diagnostic.line);
    }
    if (!diagnostic.message.empty()) {
        message += ": ";
        message += diagnostic.message;
    }
    if (!diagnostic.text.empty()) {
        message += " [";
        message += diagnostic.text;
        message += ']';
    }
    return message;
}

void AppendParseDiagnostics(ResolvedRuntimeMaterialAsset& resolved, const RenderMaterialAssetParseResult& result, kb::assets::AssetId assetId) {
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        resolved.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = ConvertSeverity(diagnostic.severity),
            .kind = RuntimeMaterialResolveDiagnosticKind::MaterialLoadFailed,
            .assetId = diagnostic.assetId.IsValid() ? diagnostic.assetId : assetId,
            .path = diagnostic.path,
            .message = ParseDiagnosticMessage(diagnostic),
        });
    }
}

[[nodiscard]] ResolvedRuntimeMaterialAsset FallbackMaterial(
    RuntimeMaterialResolveStatus status,
    RuntimeMaterialResolveDiagnosticKind kind,
    RuntimeMaterialResolveDiagnosticSeverity severity,
    kb::assets::AssetId assetId,
    std::filesystem::path path,
    std::string message) {
    ResolvedRuntimeMaterialAsset resolved{};
    resolved.material.desc = status == RuntimeMaterialResolveStatus::DefaultMaterial
        ? RuntimeMaterialResolver::DefaultMaterialDesc()
        : RuntimeMaterialResolver::ErrorMaterialDesc();
    resolved.contentHash = assetId.value;
    resolved.status = status;
    resolved.resolved = true;
    resolved.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
        .severity = severity,
        .kind = kind,
        .assetId = assetId,
        .path = std::move(path),
        .message = std::move(message),
    });
    return resolved;
}

} // namespace

std::uint64_t RuntimeMaterialResolver::EmbeddedMaterialAssetId(std::uint64_t meshAssetId, std::uint32_t slotIndex, std::string_view materialName) noexcept {
    std::string key = "RenderMeshEmbeddedMaterial:";
    key += std::to_string(meshAssetId);
    key += ':';
    key += std::to_string(slotIndex);
    key += ':';
    key += materialName;
    return kb::assets::MakeAssetId(key).value;
}

RenderMaterialDesc RuntimeMaterialResolver::DefaultMaterialDesc() noexcept {
    return RenderMaterialDesc{};
}

RenderMaterialDesc RuntimeMaterialResolver::ErrorMaterialDesc() noexcept {
    RenderMaterialDesc desc{};
    desc.baseColor[0] = 1.0F;
    desc.baseColor[1] = 0.0F;
    desc.baseColor[2] = 1.0F;
    desc.baseColor[3] = 1.0F;
    desc.roughnessFactor = 0.65F;
    return desc;
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

ResolvedRuntimeMaterialAsset RuntimeMaterialResolver::ResolveAsset(
    kb::assets::AssetManager& manager,
    kb::assets::AssetId assetId) const {
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetId);
    if (metadata == nullptr) {
        return FallbackMaterial(
            RuntimeMaterialResolveStatus::DefaultMaterial,
            RuntimeMaterialResolveDiagnosticKind::MissingMaterialAsset,
            RuntimeMaterialResolveDiagnosticSeverity::Warning,
            assetId,
            {},
            "Material asset is not registered; using the default material.");
    }
    return ResolveAsset(manager, *metadata);
}

ResolvedRuntimeMaterialAsset RuntimeMaterialResolver::ResolveAsset(
    kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata) const {
    if (metadata.type == "RenderMaterial") {
        const std::filesystem::path path = ResolveAssetPhysicalPath(manager, metadata);
        const RenderMaterialAssetParseResult loaded = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(path, metadata.id);
        if (!loaded.asset.has_value()) {
            ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
                RuntimeMaterialResolveStatus::ErrorMaterial,
                RuntimeMaterialResolveDiagnosticKind::MaterialLoadFailed,
                RuntimeMaterialResolveDiagnosticSeverity::Error,
                metadata.id,
                path,
                "Material asset could not be loaded; using the error material.");
            fallback.contentHash = metadata.contentHash;
            fallback.diagnostics.clear();
            AppendParseDiagnostics(fallback, loaded, metadata.id);
            if (fallback.diagnostics.empty()) {
                fallback.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
                    .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
                    .kind = RuntimeMaterialResolveDiagnosticKind::MaterialLoadFailed,
                    .assetId = metadata.id,
                    .path = path,
                    .message = "Material asset could not be loaded; using the error material.",
                });
            }
            return fallback;
        }

        ResolvedRuntimeMaterialAsset resolved{
            .material = ResolveLoadedMaterial(manager, metadata, *loaded.asset),
            .contentHash = metadata.contentHash,
            .status = RuntimeMaterialResolveStatus::Resolved,
            .resolved = true,
        };
        AppendParseDiagnostics(resolved, loaded, metadata.id);
        return resolved;
    }

    if (metadata.type != "RenderMaterialInstance") {
        ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
            RuntimeMaterialResolveStatus::ErrorMaterial,
            RuntimeMaterialResolveDiagnosticKind::UnsupportedAssetType,
            RuntimeMaterialResolveDiagnosticSeverity::Error,
            metadata.id,
            ResolveAssetPhysicalPath(manager, metadata),
            "Asset is not a material; using the error material.");
        fallback.contentHash = metadata.contentHash;
        return fallback;
    }

    const kb::assets::AssetHandle<RenderMaterialInstanceAssetData> instance = manager.Load<RenderMaterialInstanceAssetData>(metadata.id);
    if (!instance.IsLoaded() || !instance->parentMaterialAssetId.IsValid()) {
        ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
            RuntimeMaterialResolveStatus::ErrorMaterial,
            RuntimeMaterialResolveDiagnosticKind::MaterialInstanceLoadFailed,
            RuntimeMaterialResolveDiagnosticSeverity::Error,
            metadata.id,
            ResolveAssetPhysicalPath(manager, metadata),
            "Material instance could not be loaded or has no parent; using the error material.");
        fallback.contentHash = metadata.contentHash;
        return fallback;
    }
    const kb::assets::AssetMetadata* parentMetadata = manager.Registry().Find(instance->parentMaterialAssetId);
    if (parentMetadata == nullptr || parentMetadata->type != "RenderMaterial") {
        ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
            RuntimeMaterialResolveStatus::ErrorMaterial,
            RuntimeMaterialResolveDiagnosticKind::MissingParentMaterial,
            RuntimeMaterialResolveDiagnosticSeverity::Error,
            metadata.id,
            ResolveAssetPhysicalPath(manager, metadata),
            "Material instance parent is missing or is not a material; using the error material.");
        fallback.diagnostics.front().parentAssetId = instance->parentMaterialAssetId;
        fallback.contentHash = metadata.contentHash;
        return fallback;
    }
    ResolvedRuntimeMaterialAsset parent = ResolveAsset(manager, *parentMetadata);
    if (!parent.resolved) {
        return parent;
    }

    ResolvedRuntimeMaterialAsset resolved{
        .material = parent.material,
        .diagnostics = std::move(parent.diagnostics),
        .contentHash = HashCombine(metadata.contentHash, parentMetadata->contentHash),
        .status = parent.status,
        .resolved = true,
    };
    for (RuntimeMaterialResolveDiagnostic& diagnostic : resolved.diagnostics) {
        if (!diagnostic.parentAssetId.IsValid()) {
            diagnostic.parentAssetId = parentMetadata->id;
        }
        if (!diagnostic.assetId.IsValid()) {
            diagnostic.assetId = metadata.id;
        }
    }
    if (resolved.status == RuntimeMaterialResolveStatus::ErrorMaterial) {
        resolved.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
            .kind = RuntimeMaterialResolveDiagnosticKind::ParentMaterialLoadFailed,
            .assetId = metadata.id,
            .parentAssetId = parentMetadata->id,
            .path = ResolveAssetPhysicalPath(manager, metadata),
            .message = "Material instance parent resolved to an error material.",
        });
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
