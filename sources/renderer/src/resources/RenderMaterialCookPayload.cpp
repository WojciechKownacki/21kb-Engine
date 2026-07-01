#include "kb/render/resources/RenderMaterialCookPayload.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kb::render {
namespace {

void HashCombine(std::uint64_t& seed, std::uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

void HashString(std::uint64_t& seed, std::string_view value) noexcept {
    for (const char ch : value) {
        HashCombine(seed, static_cast<unsigned char>(ch));
    }
}

void HashFloat(std::uint64_t& seed, float value) noexcept {
    std::uint32_t bits = 0U;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(value));
    HashCombine(seed, bits);
}

[[nodiscard]] bool IsTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderTexture" || metadata.type == "Texture" || metadata.importCategory == "Texture";
}

void AppendUnique(std::vector<kb::assets::AssetId>& dependencies, kb::assets::AssetId id) {
    if (!id.IsValid()) {
        return;
    }
    for (const kb::assets::AssetId existing : dependencies) {
        if (existing == id) {
            return;
        }
    }
    dependencies.push_back(id);
}

void AppendTextureAssetDependency(
    std::vector<kb::assets::AssetId>& dependencies,
    std::uint64_t textureAssetId,
    const kb::assets::AssetRegistry& registry) {
    const kb::assets::AssetId id{ textureAssetId };
    const kb::assets::AssetMetadata* metadata = registry.Find(id);
    if (metadata != nullptr && IsTextureAsset(*metadata)) {
        AppendUnique(dependencies, id);
    }
}

void AppendTexturePathDependency(
    std::vector<kb::assets::AssetId>& dependencies,
    std::string_view texturePath,
    const kb::assets::AssetMetadata& owner,
    const kb::assets::AssetRegistry& registry) {
    if (texturePath.empty()) {
        return;
    }
    const std::filesystem::path authoredPath{ std::string{ texturePath } };
    const std::filesystem::path candidate = authoredPath.is_absolute()
        ? authoredPath.lexically_normal()
        : (owner.virtualPath.parent_path() / authoredPath).lexically_normal();
    const kb::assets::AssetMetadata* texture = registry.FindByPath(candidate);
    if (texture != nullptr && IsTextureAsset(*texture)) {
        AppendUnique(dependencies, texture->id);
    }
}

[[nodiscard]] std::vector<kb::assets::AssetId> BuildTextureDependencies(
    const RenderMaterialAssetData& material,
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) {
    std::vector<kb::assets::AssetId> dependencies;
    dependencies.reserve(13U);

    const std::array<std::uint64_t, 13U> textureAssetIds{
        material.desc.albedoTextureAssetId,
        material.desc.normalTextureAssetId,
        material.desc.metallicRoughnessTextureAssetId,
        material.desc.occlusionTextureAssetId,
        material.desc.emissiveTextureAssetId,
        material.desc.clearcoatTextureAssetId,
        material.desc.clearcoatRoughnessTextureAssetId,
        material.desc.sheenColorTextureAssetId,
        material.desc.transmissionTextureAssetId,
        material.desc.thicknessTextureAssetId,
        material.desc.anisotropyTextureAssetId,
        material.desc.decalTextureAssetId,
        material.desc.layerMaskTextureAssetId,
    };
    for (const std::uint64_t textureAssetId : textureAssetIds) {
        AppendTextureAssetDependency(dependencies, textureAssetId, registry);
    }

    const std::array<std::string_view, 13U> texturePaths{
        material.albedoTexturePath,
        material.normalTexturePath,
        material.metallicRoughnessTexturePath,
        material.occlusionTexturePath,
        material.emissiveTexturePath,
        material.clearcoatTexturePath,
        material.clearcoatRoughnessTexturePath,
        material.sheenColorTexturePath,
        material.transmissionTexturePath,
        material.thicknessTexturePath,
        material.anisotropyTexturePath,
        material.decalTexturePath,
        material.layerMaskTexturePath,
    };
    for (const std::string_view texturePath : texturePaths) {
        AppendTexturePathDependency(dependencies, texturePath, metadata, registry);
    }
    for (const RenderMaterialGraphParameterValue& value : material.graphParameterValues) {
        if (value.type == RenderMaterialParameterType::Texture) {
            AppendTextureAssetDependency(dependencies, value.assetId, registry);
        }
    }

    std::sort(dependencies.begin(), dependencies.end(), [](kb::assets::AssetId lhs, kb::assets::AssetId rhs) {
        return lhs.value < rhs.value;
    });
    return dependencies;
}

[[nodiscard]] bool IsGraphBackedMaterial(const RenderMaterialAssetData& material) noexcept {
    return !(material.graph.nodes.size() == 1U &&
        material.graph.links.empty() &&
        material.graph.nodes[0].kind == RenderMaterialGraphNodeKind::MaterialOutput);
}

[[nodiscard]] std::vector<RenderMaterialGraphDependencyHashInput> BuildGraphDependencyHashes(
    const std::vector<kb::assets::AssetId>& dependencies,
    const kb::assets::AssetRegistry& registry) {
    std::vector<RenderMaterialGraphDependencyHashInput> graphDependencies;
    graphDependencies.reserve(dependencies.size());
    for (const kb::assets::AssetId id : dependencies) {
        const kb::assets::AssetMetadata* metadata = registry.Find(id);
        if (metadata == nullptr) {
            continue;
        }
        graphDependencies.push_back(RenderMaterialGraphDependencyHashInput{
            .assetId = id.value,
            .contentHash = metadata->contentHash,
            .name = metadata->virtualPath.generic_string(),
        });
    }
    return graphDependencies;
}

void HashDesc(std::uint64_t& hash, const RenderMaterialDesc& desc) noexcept {
    for (const float value : desc.baseColor) HashFloat(hash, value);
    for (const float value : desc.emissiveColor) HashFloat(hash, value);
    HashFloat(hash, desc.metallicFactor);
    HashFloat(hash, desc.roughnessFactor);
    HashFloat(hash, desc.normalScale);
    HashFloat(hash, desc.occlusionStrength);
    HashFloat(hash, desc.emissiveStrength);
    HashFloat(hash, desc.alphaCutoff);
    for (const float value : desc.uvTiling) HashFloat(hash, value);
    for (const float value : desc.uvOffset) HashFloat(hash, value);
    HashCombine(hash, static_cast<std::uint64_t>(desc.alphaMode));
    HashCombine(hash, static_cast<std::uint64_t>(desc.translucencyBlend));
    HashCombine(hash, desc.doubleSided ? 1U : 0U);
    HashCombine(hash, desc.writesDepth ? 1U : 0U);
}

void HashGraphParameterValues(std::uint64_t& hash, const std::vector<RenderMaterialGraphParameterValue>& values) noexcept {
    for (const RenderMaterialGraphParameterValue& value : values) {
        HashString(hash, value.stableId);
        HashCombine(hash, static_cast<std::uint64_t>(value.type));
        for (const float number : value.numbers) {
            HashFloat(hash, number);
        }
        HashCombine(hash, value.assetId);
        HashCombine(hash, value.boolValue ? 1U : 0U);
        HashString(hash, value.text);
    }
}

[[nodiscard]] std::uint64_t BuildPayloadHash(const RenderMaterialCookPayload& payload) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    HashString(hash, payload.materialType);
    HashCombine(hash, payload.materialTypeVersion);
    HashCombine(hash, payload.materialTypeAssetId.value);
    HashString(hash, payload.materialTypeAssetPath);
    HashDesc(hash, payload.params);
    HashGraphParameterValues(hash, payload.graphParameterValues);
    for (const kb::assets::AssetId textureId : payload.textureDependencies) {
        HashCombine(hash, textureId.value);
    }
    HashCombine(hash, payload.graphBacked ? 1U : 0U);
    HashCombine(hash, payload.graphCompileSucceeded ? 1U : 0U);
    HashCombine(hash, payload.graphCompileKey.combinedHash);
    HashCombine(hash, payload.graphShader.sourceHash);
    HashString(hash, payload.graphShader.entryPoint);
    HashString(hash, payload.graphShader.source);
    return hash;
}

[[nodiscard]] std::uint64_t BuildManifestHash(const std::vector<RenderMaterialCookManifestEntry>& entries) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const RenderMaterialCookManifestEntry& entry : entries) {
        HashCombine(hash, entry.materialAssetId.value);
        HashString(hash, entry.materialType);
        HashCombine(hash, entry.materialTypeVersion);
        HashCombine(hash, entry.materialTypeAssetId.value);
        HashString(hash, entry.materialTypeAssetPath);
        HashCombine(hash, entry.sourceContentHash);
        HashCombine(hash, entry.payloadHash);
        for (const kb::assets::AssetId textureId : entry.textureDependencies) {
            HashCombine(hash, textureId.value);
        }
    }
    return hash;
}

} // namespace

RenderMaterialCookPayload RenderMaterialCookPayloadBuilder::Build(
    const RenderMaterialAssetData& material,
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) {
    RenderMaterialCookPayload payload{};
    payload.materialType = material.materialType.empty() ? kRenderMaterialAssetBuiltInPbrType : material.materialType;
    payload.materialTypeVersion = material.materialTypeVersion == 0U ? kRenderMaterialAssetBuiltInPbrTypeVersion : material.materialTypeVersion;
    payload.materialTypeAssetId = kb::assets::AssetId{ material.materialTypeAssetId };
    payload.materialTypeAssetPath = material.materialTypeAssetPath;
    payload.sourceContentHash = metadata.contentHash;
    payload.params = material.desc;
    payload.graphParameterValues = material.graphParameterValues;
    payload.textureDependencies = BuildTextureDependencies(material, metadata, registry);
    payload.graphBacked = IsGraphBackedMaterial(material);
    if (payload.graphBacked) {
        const std::vector<RenderMaterialGraphDependencyHashInput> graphDependencies = BuildGraphDependencyHashes(payload.textureDependencies, registry);
        payload.graphCompileKey = BuildRenderMaterialGraphCompileArtifactCacheKey(material.graph, graphDependencies, 0U);
        const RenderMaterialGraphCompileResult graphCompile = CompileRenderMaterialGraphToShaderSource(
            material.graph,
            RenderMaterialGraphBuildContext{
                .assetId = metadata.id.value,
                .sourcePath = metadata.virtualPath.generic_string(),
            });
        payload.graphCompileSucceeded = graphCompile.Succeeded();
        payload.graphShader = graphCompile.shader;
        payload.graphDiagnostics = graphCompile.diagnostics;
    }
    payload.payloadHash = BuildPayloadHash(payload);
    return payload;
}

RenderMaterialCookManifest RenderMaterialCookManifestBuilder::Build(
    std::span<const RenderMaterialCookManifestInput> materials,
    const kb::assets::AssetRegistry& registry) {
    RenderMaterialCookManifest manifest{};
    manifest.entries.reserve(materials.size());
    for (const RenderMaterialCookManifestInput& input : materials) {
        if (input.material == nullptr || input.metadata == nullptr || !input.metadata->id.IsValid()) {
            continue;
        }
        const RenderMaterialCookPayload payload = RenderMaterialCookPayloadBuilder::Build(*input.material, *input.metadata, registry);
        manifest.entries.push_back(RenderMaterialCookManifestEntry{
            .materialAssetId = input.metadata->id,
            .materialType = payload.materialType,
            .materialTypeVersion = payload.materialTypeVersion,
            .materialTypeAssetId = payload.materialTypeAssetId,
            .materialTypeAssetPath = payload.materialTypeAssetPath,
            .sourceContentHash = payload.sourceContentHash,
            .payloadHash = payload.payloadHash,
            .textureDependencies = payload.textureDependencies,
        });
    }
    std::sort(manifest.entries.begin(), manifest.entries.end(), [](const RenderMaterialCookManifestEntry& lhs, const RenderMaterialCookManifestEntry& rhs) {
        return lhs.materialAssetId.value < rhs.materialAssetId.value;
    });
    manifest.manifestHash = BuildManifestHash(manifest.entries);
    return manifest;
}

} // namespace kb::render
