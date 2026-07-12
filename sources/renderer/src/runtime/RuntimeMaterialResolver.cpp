#include "kb/render/runtime/RuntimeMaterialResolver.hpp"

#include "runtime/RuntimeMaterialGraphDependencyLoader.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialNumericParsing.hpp"
#include "kb/render/resources/RenderMaterialGraphProgramBindingBuilder.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::render {
namespace {

struct MaterialGraphRuntimeValue;

[[nodiscard]] std::uint64_t HashCombine(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return lhs ^ (rhs + 0x9e3779b97f4a7c15ULL + (lhs << 6U) + (lhs >> 2U));
}

[[nodiscard]] std::filesystem::path ResolveAssetPhysicalPath(
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata) {
    return ResolveRuntimeMaterialAssetPhysicalPath(manager, metadata);
}

[[nodiscard]] std::uint64_t MaterialRuntimeContentHashImpl(
    kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata,
    std::unordered_set<std::uint64_t>& visited);

[[nodiscard]] RuntimeMaterialResolveDiagnosticSeverity ConvertSeverity(RenderMaterialAssetParseDiagnosticSeverity severity) noexcept {
    return severity == RenderMaterialAssetParseDiagnosticSeverity::Warning
        ? RuntimeMaterialResolveDiagnosticSeverity::Warning
        : RuntimeMaterialResolveDiagnosticSeverity::Error;
}

[[nodiscard]] float FastAtanScalar(float value) noexcept {
    const float ax = std::abs(value);
    const float t = ax >= 1.0F ? 1.0F / std::max(ax, 0.000001F) : ax;
    const float s = t * t;
    float result = (((-0.0464964749F * s + 0.15931422F) * s - 0.327622764F) * s + 0.999787841F) * t;
    if (ax >= 1.0F) {
        result = 1.57079632679F - result;
    }
    return value < 0.0F ? -result : result;
}

[[nodiscard]] float FastAtan2Scalar(float y, float x) noexcept {
    if (std::abs(x) <= 0.000001F) {
        return y < 0.0F ? -1.57079632679F : 1.57079632679F;
    }
    float angle = FastAtanScalar(y / x);
    if (x < 0.0F) {
        angle += y < 0.0F ? -3.14159265359F : 3.14159265359F;
    }
    return angle;
}

[[nodiscard]] float FastAsinScalar(float value) noexcept {
    const float clamped = std::clamp(value, -1.0F, 1.0F);
    return FastAtan2Scalar(clamped, std::sqrt(std::max(0.0F, 1.0F - clamped * clamped)));
}

[[nodiscard]] float FastAcosScalar(float value) noexcept {
    return 1.57079632679F - FastAsinScalar(value);
}

[[nodiscard]] float FractScalar(float value) noexcept {
    return value - std::floor(value);
}

[[nodiscard]] std::uint32_t SobolSeedBits(float value) noexcept {
    const float seed = FractScalar(std::abs(value));
    return static_cast<std::uint32_t>(std::floor(seed * 65536.0F)) & 0xffffU;
}

[[nodiscard]] float SobolComponentToUnit(std::uint32_t value) noexcept {
    return static_cast<float>(value & 0xffffU) / 65536.0F;
}

[[nodiscard]] MaterialGraphRuntimeValue SobolRuntimeValue(
    const MaterialGraphRuntimeValue& cell,
    const MaterialGraphRuntimeValue& index,
    const MaterialGraphRuntimeValue& seed) noexcept;

[[nodiscard]] bool IsSafeTextureReferencePath(const std::filesystem::path& texturePath) {
    if (texturePath.empty() || texturePath.has_root_name()) {
        return false;
    }
    for (const std::filesystem::path& part : texturePath) {
        if (part == "." || part == ".." || part.empty()) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint64_t MaterialRuntimeContentHashImpl(
    kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata,
    std::unordered_set<std::uint64_t>& visited) {
    std::uint64_t hash = metadata.contentHash;
    if (!visited.insert(metadata.id.value).second) {
        return HashCombine(hash, 0x6d6174696e737443ULL);
    }
    if (metadata.type == "RenderMaterialInstance") {
        const kb::assets::AssetHandle<RenderMaterialInstanceAssetData> instance = manager.Load<RenderMaterialInstanceAssetData>(metadata.id);
        if (!instance.IsLoaded() || !instance->parentMaterialAssetId.IsValid()) {
            visited.erase(metadata.id.value);
            return hash;
        }
        const kb::assets::AssetMetadata* parentMetadata = manager.Registry().Find(instance->parentMaterialAssetId);
        const std::uint64_t resolvedHash = parentMetadata == nullptr ? hash : HashCombine(hash, MaterialRuntimeContentHashImpl(manager, *parentMetadata, visited));
        visited.erase(metadata.id.value);
        return resolvedHash;
    }
    if (metadata.type == "RenderMaterial" || metadata.type == kRenderMaterialFunctionAssetType) {
        for (const kb::assets::AssetId dependency : metadata.dependencies) {
            const kb::assets::AssetMetadata* dependencyMetadata = manager.Registry().Find(dependency);
            if (dependencyMetadata == nullptr || dependencyMetadata->type == "RenderTexture") {
                continue;
            }
            hash = HashCombine(hash, dependencyMetadata->id.value);
            hash = HashCombine(hash, MaterialRuntimeContentHashImpl(manager, *dependencyMetadata, visited));
        }
    }
    visited.erase(metadata.id.value);
    return hash;
}

[[nodiscard]] bool IsRuntimeTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderTexture" || metadata.type == "Texture" || metadata.importCategory == "Texture";
}

void AppendParseDiagnostics(ResolvedRuntimeMaterialAsset& resolved, const RenderMaterialAssetParseResult& result, kb::assets::AssetId assetId) {
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        resolved.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = ConvertSeverity(diagnostic.severity),
            .kind = RuntimeMaterialResolveDiagnosticKind::MaterialLoadFailed,
            .assetId = diagnostic.assetId.IsValid() ? diagnostic.assetId : assetId,
            .path = diagnostic.path,
            .message = RuntimeMaterialParseDiagnosticMessage(diagnostic),
        });
    }
}

void AppendMaterialTypeReferenceDiagnostics(
    ResolvedRuntimeMaterialAsset& resolved,
    const RenderMaterialTypeReferenceValidationResult& validation,
    kb::assets::AssetId materialAssetId,
    const std::filesystem::path& materialPath) {
    for (const RenderMaterialTypeReferenceDiagnostic& diagnostic : validation.diagnostics) {
        resolved.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
            .kind = RuntimeMaterialResolveDiagnosticKind::MaterialTypeReferenceValidationFailed,
            .assetId = materialAssetId,
            .parentAssetId = diagnostic.assetId,
            .path = diagnostic.path.empty() ? materialPath : diagnostic.path,
            .message = std::string{ RenderMaterialTypeReferenceDiagnosticCodeName(diagnostic.code) } + ": " + diagnostic.message,
        });
    }
}

void AppendGraphValidationDiagnostics(
    ResolvedRuntimeMaterialAsset& resolved,
    const std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    kb::assets::AssetId materialAssetId,
    const std::filesystem::path& materialPath) {
    for (const RenderMaterialGraphDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity != RenderMaterialGraphDiagnosticSeverity::Error) {
            continue;
        }
        std::string message = std::string{ RenderMaterialGraphDiagnosticKindName(diagnostic.kind) };
        if (diagnostic.nodeId != 0U) {
            message += " node ";
            message += std::to_string(diagnostic.nodeId);
        }
        if (!diagnostic.pin.empty()) {
            message += " pin ";
            message += diagnostic.pin;
        }
        if (!diagnostic.message.empty()) {
            message += ": ";
            message += diagnostic.message;
        }
        resolved.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
            .kind = RuntimeMaterialResolveDiagnosticKind::MaterialGraphValidationFailed,
            .assetId = materialAssetId,
            .path = materialPath,
            .message = std::move(message),
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
    const RuntimeFallbackMaterialProfile profile = RuntimeMaterialResolver::FallbackMaterialProfile(
        status == RuntimeMaterialResolveStatus::DefaultMaterial ? RuntimeFallbackMaterialKind::Default : RuntimeFallbackMaterialKind::Error);
    resolved.material.desc = profile.desc;
    resolved.contentHash = assetId.value;
    resolved.status = profile.status;
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

struct EffectiveRuntimeMaterialDocumentResult {
    std::optional<RenderMaterialAssetData> material;
    std::vector<RuntimeMaterialResolveDiagnostic> diagnostics;
};

void AppendParentMaterialParseDiagnostics(
    EffectiveRuntimeMaterialDocumentResult& result,
    const RenderMaterialAssetParseResult& parseResult,
    kb::assets::AssetId ownerAssetId,
    kb::assets::AssetId parentAssetId) {
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : parseResult.diagnostics) {
        result.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = ConvertSeverity(diagnostic.severity),
            .kind = RuntimeMaterialResolveDiagnosticKind::ParentMaterialLoadFailed,
            .assetId = ownerAssetId,
            .parentAssetId = parentAssetId,
            .path = diagnostic.path,
            .message = RuntimeMaterialParseDiagnosticMessage(diagnostic),
        });
    }
}

void AppendInstanceValidationDiagnostics(
    EffectiveRuntimeMaterialDocumentResult& result,
    const RenderMaterialInstanceValidationResult& validation,
    kb::assets::AssetId ownerAssetId,
    kb::assets::AssetId parentAssetId,
    const std::filesystem::path& path) {
    for (const RenderMaterialInstanceValidationDiagnostic& diagnostic : validation.diagnostics) {
        result.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
            .kind = RuntimeMaterialResolveDiagnosticKind::MaterialInstanceValidationFailed,
            .assetId = ownerAssetId,
            .parentAssetId = parentAssetId,
            .path = path,
            .message = std::string{ RenderMaterialInstanceValidationDiagnosticCodeName(diagnostic.code) } + ": " + diagnostic.message,
        });
    }
}

[[nodiscard]] EffectiveRuntimeMaterialDocumentResult LoadEffectiveRuntimeMaterialDocument(
    kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata,
    std::unordered_set<std::uint64_t>& visited) {
    EffectiveRuntimeMaterialDocumentResult result{};
    if (!visited.insert(metadata.id.value).second) {
        result.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
            .kind = RuntimeMaterialResolveDiagnosticKind::MaterialInstanceValidationFailed,
            .assetId = metadata.id,
            .path = ResolveAssetPhysicalPath(manager, metadata),
            .message = "Material instance parent chain contains a cycle.",
        });
        return result;
    }

    if (metadata.type == "RenderMaterial") {
        const std::filesystem::path path = ResolveAssetPhysicalPath(manager, metadata);
        RenderMaterialAssetParseResult loaded = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(path, metadata.id);
        if (!loaded.asset.has_value()) {
            AppendParentMaterialParseDiagnostics(result, loaded, metadata.id, metadata.id);
            visited.erase(metadata.id.value);
            return result;
        }
        result.material = *loaded.asset;
        visited.erase(metadata.id.value);
        return result;
    }

    if (metadata.type != "RenderMaterialInstance") {
        result.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
            .kind = RuntimeMaterialResolveDiagnosticKind::UnsupportedAssetType,
            .assetId = metadata.id,
            .path = ResolveAssetPhysicalPath(manager, metadata),
            .message = "Material instance parent is neither a material nor a material instance.",
        });
        visited.erase(metadata.id.value);
        return result;
    }

    const kb::assets::AssetHandle<RenderMaterialInstanceAssetData> instance = manager.Load<RenderMaterialInstanceAssetData>(metadata.id);
    if (!instance.IsLoaded() || !instance->parentMaterialAssetId.IsValid()) {
        result.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
            .kind = RuntimeMaterialResolveDiagnosticKind::MaterialInstanceLoadFailed,
            .assetId = metadata.id,
            .path = ResolveAssetPhysicalPath(manager, metadata),
            .message = "Material instance could not be loaded or has no parent.",
        });
        visited.erase(metadata.id.value);
        return result;
    }

    const kb::assets::AssetMetadata* parentMetadata = manager.Registry().Find(instance->parentMaterialAssetId);
    if (parentMetadata == nullptr || (parentMetadata->type != "RenderMaterial" && parentMetadata->type != "RenderMaterialInstance")) {
        result.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
            .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
            .kind = RuntimeMaterialResolveDiagnosticKind::MissingParentMaterial,
            .assetId = metadata.id,
            .parentAssetId = instance->parentMaterialAssetId,
            .path = ResolveAssetPhysicalPath(manager, metadata),
            .message = "Material instance parent is missing or is not a material.",
        });
        visited.erase(metadata.id.value);
        return result;
    }

    EffectiveRuntimeMaterialDocumentResult parent = LoadEffectiveRuntimeMaterialDocument(manager, *parentMetadata, visited);
    if (!parent.material.has_value()) {
        result.diagnostics = std::move(parent.diagnostics);
        for (RuntimeMaterialResolveDiagnostic& diagnostic : result.diagnostics) {
            if (!diagnostic.assetId.IsValid()) {
                diagnostic.assetId = metadata.id;
            }
            if (!diagnostic.parentAssetId.IsValid()) {
                diagnostic.parentAssetId = parentMetadata->id;
            }
        }
        visited.erase(metadata.id.value);
        return result;
    }

    const RenderMaterialInstanceValidationResult validation =
        RenderMaterialInstanceAssetLoader::ValidateAgainstParent(*instance, *parent.material);
    if (!validation.Succeeded()) {
        AppendInstanceValidationDiagnostics(result, validation, metadata.id, parentMetadata->id, ResolveAssetPhysicalPath(manager, metadata));
        visited.erase(metadata.id.value);
        return result;
    }

    result.material = BuildEffectiveRenderMaterialInstanceAsset(*parent.material, *instance);
    result.diagnostics = std::move(parent.diagnostics);
    visited.erase(metadata.id.value);
    return result;
}

[[nodiscard]] std::string NormalizeMaterialParameterKey(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) != 0) {
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return normalized;
}

[[nodiscard]] bool IsAnyMaterialParameterKey(std::string_view key, std::initializer_list<std::string_view> aliases) noexcept {
    return std::any_of(aliases.begin(), aliases.end(), [key](std::string_view alias) {
        return key == alias;
    });
}

[[nodiscard]] std::optional<RenderMaterialGraphTextureDimension> GraphTextureDimensionForStableId(
    const RenderMaterialGraphDocument& graph,
    std::string_view stableId) {
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        bool stableIdMatches = node.parameter.stableId == stableId;
        if (node.parameter.stableId.empty()) {
            const std::string suffix = std::to_string(node.id);
            switch (node.kind) {
            case RenderMaterialGraphNodeKind::ParameterTexture: stableIdMatches = stableId == "texture" + suffix; break;
            case RenderMaterialGraphNodeKind::TextureSample: stableIdMatches = stableId == "textureSample" + suffix; break;
            case RenderMaterialGraphNodeKind::TextureObject: stableIdMatches = stableId == "textureObject" + suffix; break;
            case RenderMaterialGraphNodeKind::TextureSampleCube: stableIdMatches = stableId == "textureSampleCube" + suffix; break;
            case RenderMaterialGraphNodeKind::TextureObjectCube: stableIdMatches = stableId == "textureObjectCube" + suffix; break;
            case RenderMaterialGraphNodeKind::TextureSampleVolume: stableIdMatches = stableId == "textureSampleVolume" + suffix; break;
            case RenderMaterialGraphNodeKind::TextureObjectVolume: stableIdMatches = stableId == "textureObjectVolume" + suffix; break;
            case RenderMaterialGraphNodeKind::TextureSample2DArray: stableIdMatches = stableId == "textureSample2DArray" + suffix; break;
            case RenderMaterialGraphNodeKind::TextureObject2DArray: stableIdMatches = stableId == "textureObject2DArray" + suffix; break;
            default: stableIdMatches = false; break;
            }
        }
        if (!stableIdMatches) {
            continue;
        }
        switch (node.kind) {
        case RenderMaterialGraphNodeKind::TextureSampleCube:
        case RenderMaterialGraphNodeKind::TextureObjectCube:
            return RenderMaterialGraphTextureDimension::TextureCube;
        case RenderMaterialGraphNodeKind::TextureSampleVolume:
        case RenderMaterialGraphNodeKind::TextureObjectVolume:
            return RenderMaterialGraphTextureDimension::Texture3D;
        case RenderMaterialGraphNodeKind::TextureSample2DArray:
        case RenderMaterialGraphNodeKind::TextureObject2DArray:
            return RenderMaterialGraphTextureDimension::Texture2DArray;
        case RenderMaterialGraphNodeKind::ParameterTexture:
        case RenderMaterialGraphNodeKind::TextureSample:
        case RenderMaterialGraphNodeKind::TextureObject:
            return RenderMaterialGraphTextureDimension::Texture2D;
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void ApplyTextureAssetIdByRole(RenderMaterialDesc& desc, std::string_view role, std::uint64_t assetId) noexcept {
    const std::string key = NormalizeMaterialParameterKey(role);
    if (IsAnyMaterialParameterKey(key, { "basecolor", "albedo" })) {
        desc.albedoTextureAssetId = assetId;
    } else if (IsAnyMaterialParameterKey(key, { "normal", "normalmap" })) {
        desc.normalTextureAssetId = assetId;
    } else if (IsAnyMaterialParameterKey(key, { "metallicroughness", "orm", "rmo" })) {
        desc.metallicRoughnessTextureAssetId = assetId;
    } else if (IsAnyMaterialParameterKey(key, { "occlusion", "ao" })) {
        desc.occlusionTextureAssetId = assetId;
    } else if (key == "emissive") {
        desc.emissiveTextureAssetId = assetId;
    } else if (key == "clearcoat") {
        desc.clearcoatTextureAssetId = assetId;
    } else if (key == "clearcoatroughness") {
        desc.clearcoatRoughnessTextureAssetId = assetId;
    } else if (key == "sheencolor") {
        desc.sheenColorTextureAssetId = assetId;
    } else if (key == "transmission") {
        desc.transmissionTextureAssetId = assetId;
    } else if (key == "thickness") {
        desc.thicknessTextureAssetId = assetId;
    } else if (key == "anisotropy") {
        desc.anisotropyTextureAssetId = assetId;
    } else if (key == "decal") {
        desc.decalTextureAssetId = assetId;
    } else if (key == "layermask") {
        desc.layerMaskTextureAssetId = assetId;
    }
}

void ApplyGraphParameterValuesToPbrDesc(RenderMaterialDesc& desc, const RenderMaterialAssetData& materialAsset) {
    for (const RenderMaterialGraphParameterValue& value : materialAsset.graphParameterValues) {
        const std::string key = NormalizeMaterialParameterKey(value.stableId);
        switch (value.type) {
        case RenderMaterialParameterType::Scalar:
            if (IsAnyMaterialParameterKey(key, { "metallic", "metallicfactor" })) {
                desc.metallicFactor = std::clamp(value.numbers[0], 0.0F, 1.0F);
            } else if (IsAnyMaterialParameterKey(key, { "roughness", "roughnessfactor" })) {
                desc.roughnessFactor = std::clamp(value.numbers[0], 0.0F, 1.0F);
            } else if (IsAnyMaterialParameterKey(key, { "normalscale" })) {
                desc.normalScale = std::clamp(value.numbers[0], 0.0F, 8.0F);
            } else if (IsAnyMaterialParameterKey(key, { "occlusion", "occlusionstrength", "aostrength" })) {
                desc.occlusionStrength = std::clamp(value.numbers[0], 0.0F, 1.0F);
            } else if (IsAnyMaterialParameterKey(key, { "emissivestrength" })) {
                desc.emissiveStrength = std::max(value.numbers[0], 0.0F);
            } else if (IsAnyMaterialParameterKey(key, { "alphacutoff", "cutoff" })) {
                desc.alphaCutoff = std::clamp(value.numbers[0], 0.0F, 1.0F);
            }
            break;
        case RenderMaterialParameterType::Vec3:
            if (IsAnyMaterialParameterKey(key, { "emissive", "emissivecolor", "emissivefactor" })) {
                for (std::size_t channel = 0U; channel < 3U; ++channel) {
                    desc.emissiveColor[channel] = std::max(value.numbers[channel], 0.0F);
                }
            }
            break;
        case RenderMaterialParameterType::Vec4:
        case RenderMaterialParameterType::Color:
            if (IsAnyMaterialParameterKey(key, { "basecolor", "basecolorfactor", "albedo", "albedocolor", "tint", "tintcolor" })) {
                for (std::size_t channel = 0U; channel < 4U; ++channel) {
                    desc.baseColor[channel] = std::clamp(value.numbers[channel], 0.0F, 1.0F);
                }
            } else if (IsAnyMaterialParameterKey(key, { "emissive", "emissivecolor", "emissivefactor" })) {
                for (std::size_t channel = 0U; channel < 3U; ++channel) {
                    desc.emissiveColor[channel] = std::max(value.numbers[channel], 0.0F);
                }
            }
            break;
        case RenderMaterialParameterType::Texture:
            if (const std::optional<RenderMaterialGraphTextureDimension> dimension =
                    GraphTextureDimensionForStableId(materialAsset.graph, value.stableId);
                dimension.has_value() && *dimension != RenderMaterialGraphTextureDimension::Texture2D) {
                // Legacy PBR texture fields are sampler2D-only. Non-2D assets stay exclusively in
                // graphProgram.textures, where their reflected sampler dimension is preserved.
                break;
            }
            if (IsAnyMaterialParameterKey(key, { "basecolor", "basecolortexture", "albedo", "albedotexture" })) {
                desc.albedoTextureAssetId = value.assetId;
            } else if (IsAnyMaterialParameterKey(key, { "normal", "normalmap", "normaltexture" })) {
                desc.normalTextureAssetId = value.assetId;
            } else if (IsAnyMaterialParameterKey(key, { "metallicroughness", "metallicroughnesstexture", "orm", "rmo" })) {
                desc.metallicRoughnessTextureAssetId = value.assetId;
            } else if (IsAnyMaterialParameterKey(key, { "occlusion", "occlusiontexture", "ao", "aotexture" })) {
                desc.occlusionTextureAssetId = value.assetId;
            } else if (IsAnyMaterialParameterKey(key, { "emissive", "emissivetexture" })) {
                desc.emissiveTextureAssetId = value.assetId;
            }
            break;
        case RenderMaterialParameterType::Bool:
        case RenderMaterialParameterType::Enum:
            break;
        }
    }
}

void ApplyGraphTextureSlotValuesToPbrDesc(RenderMaterialDesc& desc, const RenderMaterialAssetData& materialAsset) {
    if (!HasGraphAuthoringData(materialAsset.graph) || materialAsset.graphParameterValues.empty()) {
        return;
    }

    const RenderMaterialTypeSchema schema = BuildRenderMaterialGraphParameterSchema(materialAsset.graph, "runtime.graph.preview", 1U);
    for (const RenderMaterialTextureSlotSchema& slot : schema.textureSlots) {
        if (slot.role.empty() || slot.assetIdFieldName.empty()) {
            continue;
        }
        for (const RenderMaterialGraphParameterValue& value : materialAsset.graphParameterValues) {
            if (value.type != RenderMaterialParameterType::Texture) {
                continue;
            }
            if (slot.assetIdFieldName == value.stableId + "TextureAssetId") {
                const std::optional<RenderMaterialGraphTextureDimension> dimension =
                    GraphTextureDimensionForStableId(materialAsset.graph, value.stableId);
                if (!dimension.has_value() || *dimension == RenderMaterialGraphTextureDimension::Texture2D) {
                    ApplyTextureAssetIdByRole(desc, slot.role, value.assetId);
                }
                break;
            }
        }
    }
}

[[nodiscard]] const RenderMaterialGraphLink* FindGraphInputLink(
    const RenderMaterialGraphDocument& graph,
    std::uint32_t nodeId,
    std::string_view pin) noexcept {
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (link.toNodeId == nodeId && link.toPin == pin) {
            return &link;
        }
    }
    return nullptr;
}

[[nodiscard]] bool IsRuntimeTextureObjectNode(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ParameterTexture:
    case RenderMaterialGraphNodeKind::TextureObject:
    case RenderMaterialGraphNodeKind::TextureObjectCube:
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::string StableGraphParameterIdForRuntime(const RenderMaterialGraphNode& node) {
    if (!node.parameter.stableId.empty()) {
        return node.parameter.stableId;
    }
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return "texture" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSample:
        return "textureSample" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureObject:
        return "textureObject" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSampleCube:
        return "textureSampleCube" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureObjectCube:
        return "textureObjectCube" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSampleVolume:
        return "textureSampleVolume" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureObjectVolume:
        return "textureObjectVolume" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSample2DArray:
        return "textureSample2DArray" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureObject2DArray:
        return "textureObject2DArray" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return "scalar" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterVector:
        return "vector" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterColor:
        return "color" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::CollectionParameter:
        return "collectionParam" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector2:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Subtract:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Divide:
    case RenderMaterialGraphNodeKind::Power:
    case RenderMaterialGraphNodeKind::OneMinus:
    case RenderMaterialGraphNodeKind::Absolute:
    case RenderMaterialGraphNodeKind::Minimum:
    case RenderMaterialGraphNodeKind::Maximum:
    case RenderMaterialGraphNodeKind::Saturate:
    case RenderMaterialGraphNodeKind::Floor:
    case RenderMaterialGraphNodeKind::Ceil:
    case RenderMaterialGraphNodeKind::Fraction:
    case RenderMaterialGraphNodeKind::SquareRoot:
    case RenderMaterialGraphNodeKind::Sine:
    case RenderMaterialGraphNodeKind::Cosine:
    case RenderMaterialGraphNodeKind::DotProduct:
    case RenderMaterialGraphNodeKind::CrossProduct:
    case RenderMaterialGraphNodeKind::Normalize:
    case RenderMaterialGraphNodeKind::Length:
    case RenderMaterialGraphNodeKind::Distance:
    case RenderMaterialGraphNodeKind::BreakVector:
    case RenderMaterialGraphNodeKind::MakeVector:
    case RenderMaterialGraphNodeKind::Step:
    case RenderMaterialGraphNodeKind::SmoothStep:
    case RenderMaterialGraphNodeKind::If:
    case RenderMaterialGraphNodeKind::RuntimeSwitch:
    case RenderMaterialGraphNodeKind::Desaturate:
    case RenderMaterialGraphNodeKind::Fresnel:
    case RenderMaterialGraphNodeKind::Negate:
    case RenderMaterialGraphNodeKind::Sign:
    case RenderMaterialGraphNodeKind::Round:
    case RenderMaterialGraphNodeKind::Truncate:
    case RenderMaterialGraphNodeKind::Tangent:
    case RenderMaterialGraphNodeKind::ArcSine:
    case RenderMaterialGraphNodeKind::ArcCosine:
    case RenderMaterialGraphNodeKind::ArcTangent:
    case RenderMaterialGraphNodeKind::ArcTangent2:
    case RenderMaterialGraphNodeKind::ArcSineFast:
    case RenderMaterialGraphNodeKind::ArcCosineFast:
    case RenderMaterialGraphNodeKind::ArcTangentFast:
    case RenderMaterialGraphNodeKind::ArcTangent2Fast:
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
    case RenderMaterialGraphNodeKind::Sobol:
    case RenderMaterialGraphNodeKind::Uv:
        break;
    }
    return "parameter" + std::to_string(node.id);
}

[[nodiscard]] std::optional<std::uint64_t> GraphTextureValueAssetId(
    const std::vector<RenderMaterialGraphParameterValue>& values,
    std::string_view stableId) noexcept {
    for (const RenderMaterialGraphParameterValue& value : values) {
        if (value.stableId == stableId && value.type == RenderMaterialParameterType::Texture) {
            return value.assetId;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> TextureAssetIdForTextureSampleNode(
    const RenderMaterialAssetData& materialAsset,
    const RenderMaterialGraphNode& textureSample) noexcept {
    if (const RenderMaterialGraphLink* textureInput = FindGraphInputLink(materialAsset.graph, textureSample.id, "texture");
        textureInput != nullptr) {
        const RenderMaterialGraphNode* source = FindRenderMaterialGraphNode(materialAsset.graph, textureInput->fromNodeId);
        if (source != nullptr && IsRuntimeTextureObjectNode(source->kind)) {
            return GraphTextureValueAssetId(materialAsset.graphParameterValues, StableGraphParameterIdForRuntime(*source));
        }
        return std::nullopt;
    }
    return GraphTextureValueAssetId(materialAsset.graphParameterValues, StableGraphParameterIdForRuntime(textureSample));
}

struct MaterialGraphRuntimeValue {
    std::array<float, 4U> value{ 0.0F, 0.0F, 0.0F, 1.0F };
    std::uint64_t textureAssetId = 0U;
    RenderMaterialGraphPinType type = RenderMaterialGraphPinType::Unknown;
    bool authored = false;
};

[[nodiscard]] MaterialGraphRuntimeValue RuntimeValue(float value, bool authored = true) noexcept {
    return MaterialGraphRuntimeValue{
        .value = { value, value, value, value },
        .type = RenderMaterialGraphPinType::Float,
        .authored = authored,
    };
}

[[nodiscard]] MaterialGraphRuntimeValue RuntimeValue(float x, float y, float z, float w, RenderMaterialGraphPinType type, bool authored = true) noexcept {
    return MaterialGraphRuntimeValue{
        .value = { x, y, z, w },
        .type = type,
        .authored = authored,
    };
}

[[nodiscard]] float Clamp01(float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] MaterialGraphRuntimeValue SobolRuntimeValue(
    const MaterialGraphRuntimeValue& cell,
    const MaterialGraphRuntimeValue& index,
    const MaterialGraphRuntimeValue& seed) noexcept {
    static constexpr std::array<std::array<std::uint32_t, 2U>, 10U> kDirections{ {
        { 34432U, 19584U },
        { 62016U, 37440U },
        { 33312U, 3616U },
        { 16656U, 5648U },
        { 42504U, 30216U },
        { 35330U, 10250U },
        { 57860U, 40452U },
        { 41984U, 18050U },
        { 58112U, 42829U },
        { 46848U, 38935U },
    } };

    const float originX = std::floor(cell.value[0]);
    const float originY = std::floor(cell.value[1]);
    const std::uint32_t cx = static_cast<std::uint32_t>(std::floor(std::abs(cell.value[0])));
    const std::uint32_t cy = static_cast<std::uint32_t>(std::floor(std::abs(cell.value[1])));
    std::uint32_t sx = ((cx * 1973U) + (cy * 9277U)) & 0xffffU;
    std::uint32_t sy = ((cx * 26699U) + (cy * 31847U)) & 0xffffU;
    const std::uint32_t sampleIndex = static_cast<std::uint32_t>(std::floor(std::max(index.value[0], 0.0F)));
    for (std::uint32_t bit = 0U; bit < kDirections.size(); ++bit) {
        if ((sampleIndex & (1U << bit)) != 0U) {
            sx ^= kDirections[bit][0];
            sy ^= kDirections[bit][1];
        }
    }
    sx ^= SobolSeedBits(seed.value[0]);
    sy ^= SobolSeedBits(seed.value[1]);
    return RuntimeValue(
        originX + SobolComponentToUnit(sx),
        originY + SobolComponentToUnit(sy),
        0.0F,
        1.0F,
        RenderMaterialGraphPinType::Float2,
        cell.authored || index.authored || seed.authored);
}

[[nodiscard]] std::vector<float> ParseGraphDefaultNumbers(std::string_view text) {
    std::vector<float> values;
    if (text.empty() || text == "_") {
        return values;
    }
    static_cast<void>(ParseFiniteMaterialFloatSequence(text, values, 1U, 64U));
    return values;
}

[[nodiscard]] MaterialGraphRuntimeValue ConstantScalarRuntimeValue(const RenderMaterialGraphNode& node) {
    const std::vector<float> values = ParseGraphDefaultNumbers(node.parameter.defaultValueHint);
    return RuntimeValue(values.empty() ? 1.0F : values[0], !values.empty());
}

[[nodiscard]] MaterialGraphRuntimeValue ConstantVectorRuntimeValue(const RenderMaterialGraphNode& node) {
    const std::vector<float> values = ParseGraphDefaultNumbers(node.parameter.defaultValueHint);
    const float x = values.size() > 0U ? values[0] : 1.0F;
    const float y = values.size() > 1U ? values[1] : x;
    const float z = values.size() > 2U ? values[2] : y;
    return RuntimeValue(x, y, z, 1.0F, RenderMaterialGraphPinType::Float3, !values.empty());
}

[[nodiscard]] MaterialGraphRuntimeValue ConstantVector2RuntimeValue(const RenderMaterialGraphNode& node) {
    const std::vector<float> values = ParseGraphDefaultNumbers(node.parameter.defaultValueHint);
    const float x = values.size() > 0U ? values[0] : 0.0F;
    const float y = values.size() > 1U ? values[1] : x;
    return RuntimeValue(x, y, 0.0F, 1.0F, RenderMaterialGraphPinType::Float2, !values.empty());
}

[[nodiscard]] MaterialGraphRuntimeValue ConstantColorRuntimeValue(const RenderMaterialGraphNode& node) {
    const std::vector<float> values = ParseGraphDefaultNumbers(node.parameter.defaultValueHint);
    const float r = values.size() > 0U ? values[0] : 1.0F;
    const float g = values.size() > 1U ? values[1] : r;
    const float b = values.size() > 2U ? values[2] : g;
    const float a = values.size() > 3U ? values[3] : 1.0F;
    return RuntimeValue(r, g, b, a, RenderMaterialGraphPinType::Color, !values.empty());
}

[[nodiscard]] std::uint64_t MergeTextureProvenance(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    if (lhs == 0U) {
        return rhs;
    }
    if (rhs == 0U || lhs == rhs) {
        return lhs;
    }
    return 0U;
}

[[nodiscard]] float Dot3(const MaterialGraphRuntimeValue& lhs, const MaterialGraphRuntimeValue& rhs) noexcept {
    return (lhs.value[0] * rhs.value[0]) + (lhs.value[1] * rhs.value[1]) + (lhs.value[2] * rhs.value[2]);
}

[[nodiscard]] float Length3(const MaterialGraphRuntimeValue& value) noexcept {
    return std::sqrt(Dot3(value, value));
}

[[nodiscard]] MaterialGraphRuntimeValue GraphParameterRuntimeValue(const RenderMaterialGraphNode& node, const RenderMaterialAssetData& materialAsset) {
    const std::string stableId = StableGraphParameterIdForRuntime(node);
    for (const RenderMaterialGraphParameterValue& value : materialAsset.graphParameterValues) {
        if (value.stableId != stableId) {
            continue;
        }
        switch (value.type) {
        case RenderMaterialParameterType::Scalar:
            return RuntimeValue(value.numbers[0]);
        case RenderMaterialParameterType::Vec3:
            return RuntimeValue(value.numbers[0], value.numbers[1], value.numbers[2], 1.0F, RenderMaterialGraphPinType::Float3);
        case RenderMaterialParameterType::Vec4:
            return RuntimeValue(value.numbers[0], value.numbers[1], value.numbers[2], value.numbers[3], RenderMaterialGraphPinType::Float4);
        case RenderMaterialParameterType::Color:
            return RuntimeValue(value.numbers[0], value.numbers[1], value.numbers[2], value.numbers[3], RenderMaterialGraphPinType::Color);
        case RenderMaterialParameterType::Texture:
            return MaterialGraphRuntimeValue{
                .value = { 1.0F, 1.0F, 1.0F, 1.0F },
                .textureAssetId = value.assetId,
                .type = RenderMaterialGraphPinType::Texture2D,
                .authored = value.assetId != 0U,
            };
        case RenderMaterialParameterType::Bool:
            return RuntimeValue(value.boolValue ? 1.0F : 0.0F);
        case RenderMaterialParameterType::Enum:
            break;
        }
    }

    switch (node.kind) {
    case RenderMaterialGraphNodeKind::ParameterScalar: {
        const std::vector<float> values = ParseGraphDefaultNumbers(node.parameter.defaultValueHint);
        return RuntimeValue(values.empty() ? 0.0F : values[0], !values.empty());
    }
    case RenderMaterialGraphNodeKind::ParameterVector: {
        const std::vector<float> values = ParseGraphDefaultNumbers(node.parameter.defaultValueHint);
        const float x = values.size() > 0U ? values[0] : 0.0F;
        const float y = values.size() > 1U ? values[1] : x;
        const float z = values.size() > 2U ? values[2] : y;
        return RuntimeValue(x, y, z, 1.0F, RenderMaterialGraphPinType::Float3, !values.empty());
    }
    case RenderMaterialGraphNodeKind::ParameterColor: {
        const std::vector<float> values = ParseGraphDefaultNumbers(node.parameter.defaultValueHint);
        const float r = values.size() > 0U ? values[0] : 1.0F;
        const float g = values.size() > 1U ? values[1] : r;
        const float b = values.size() > 2U ? values[2] : g;
        const float a = values.size() > 3U ? values[3] : 1.0F;
        return RuntimeValue(r, g, b, a, RenderMaterialGraphPinType::Color, !values.empty());
    }
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return MaterialGraphRuntimeValue{ .type = RenderMaterialGraphPinType::Texture2D };
    default:
        break;
    }
    return {};
}

[[nodiscard]] RenderMaterialGraphDocument MaterialOutputRuntimeGraph(const RenderMaterialGraphDocument& graph) {
    const RenderMaterialGraphNode* output = nullptr;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            output = &node;
            break;
        }
    }
    if (output == nullptr) {
        return graph;
    }

    std::unordered_set<std::uint32_t> nodeIds;
    std::unordered_set<std::uint32_t> linkIds;
    std::vector<std::uint32_t> stack{ output->id };
    static_cast<void>(nodeIds.insert(output->id));
    while (!stack.empty()) {
        const std::uint32_t nodeId = stack.back();
        stack.pop_back();
        for (const RenderMaterialGraphLink& link : graph.links) {
            if (link.toNodeId != nodeId) {
                continue;
            }
            static_cast<void>(linkIds.insert(link.id));
            if (FindRenderMaterialGraphNode(graph, link.fromNodeId) != nullptr && nodeIds.insert(link.fromNodeId).second) {
                stack.push_back(link.fromNodeId);
            }
        }
    }

    RenderMaterialGraphDocument runtimeGraph = graph;
    runtimeGraph.nodes.clear();
    runtimeGraph.links.clear();
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (nodeIds.contains(node.id)) {
            runtimeGraph.nodes.push_back(node);
        }
    }
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (linkIds.contains(link.id)) {
            runtimeGraph.links.push_back(link);
        }
    }
    return runtimeGraph;
}

[[nodiscard]] std::vector<RenderMaterialGraphDiagnostic> ValidateMaterialOutputRuntimeGraphDiagnostics(const RenderMaterialAssetData& asset) {
    RenderMaterialAssetData runtimeAsset = asset;
    runtimeAsset.graph = MaterialOutputRuntimeGraph(asset.graph);
    const RenderMaterialTypeSchema runtimeSchema =
        BuildRenderMaterialGraphParameterSchema(runtimeAsset.graph, "runtime.validation", 1U);
    std::erase_if(runtimeAsset.graphParameterValues, [&runtimeSchema](const RenderMaterialGraphParameterValue& value) {
        const bool parameterExists = std::ranges::any_of(runtimeSchema.parameters, [&value](const RenderMaterialParameterSchema& parameter) {
            return parameter.name == value.stableId;
        });
        const bool textureExists = std::ranges::any_of(runtimeSchema.textureSlots, [&value](const RenderMaterialTextureSlotSchema& slot) {
            return (slot.stableId.empty() ? slot.name : slot.stableId) == value.stableId;
        });
        return !parameterExists && !textureExists;
    });
    return ValidateRenderMaterialAssetGraphDiagnostics(runtimeAsset);
}

[[nodiscard]] MaterialGraphRuntimeValue EvaluateGraphNodeOutput(
    const RenderMaterialAssetData& materialAsset,
    const RenderMaterialGraphNode& node,
    std::string_view outputPin,
    std::vector<std::uint32_t>& stack);

[[nodiscard]] MaterialGraphRuntimeValue EvaluateGraphInput(
    const RenderMaterialAssetData& materialAsset,
    const RenderMaterialGraphNode& node,
    std::string_view inputPin,
    MaterialGraphRuntimeValue fallback,
    std::vector<std::uint32_t>& stack) {
    const RenderMaterialGraphLink* link = FindGraphInputLink(materialAsset.graph, node.id, inputPin);
    if (link == nullptr) {
        return fallback;
    }
    const RenderMaterialGraphNode* source = FindRenderMaterialGraphNode(materialAsset.graph, link->fromNodeId);
    if (source == nullptr) {
        return fallback;
    }
    return EvaluateGraphNodeOutput(materialAsset, *source, link->fromPin, stack);
}

[[nodiscard]] MaterialGraphRuntimeValue EvaluateTextureSampleOutput(
    const RenderMaterialAssetData& materialAsset,
    const RenderMaterialGraphNode& node,
    std::string_view outputPin) {
    const std::uint64_t textureAssetId = TextureAssetIdForTextureSampleNode(materialAsset, node).value_or(0U);
    MaterialGraphRuntimeValue value{
        .value = { 1.0F, 1.0F, 1.0F, 1.0F },
        .textureAssetId = textureAssetId,
        .type = outputPin == "color" ? RenderMaterialGraphPinType::Color : RenderMaterialGraphPinType::Float,
        .authored = textureAssetId != 0U,
    };
    if (outputPin == "r") {
        value.value = { 1.0F, 1.0F, 1.0F, 1.0F };
    } else if (outputPin == "g") {
        value.value = { 1.0F, 1.0F, 1.0F, 1.0F };
    } else if (outputPin == "b") {
        value.value = { 1.0F, 1.0F, 1.0F, 1.0F };
    } else if (outputPin == "a") {
        value.value = { 1.0F, 1.0F, 1.0F, 1.0F };
    }
    return value;
}

[[nodiscard]] MaterialGraphRuntimeValue EvaluateGraphNodeOutput(
    const RenderMaterialAssetData& materialAsset,
    const RenderMaterialGraphNode& node,
    std::string_view outputPin,
    std::vector<std::uint32_t>& stack) {
    if (std::find(stack.begin(), stack.end(), node.id) != stack.end()) {
        return {};
    }
    stack.push_back(node.id);

    MaterialGraphRuntimeValue result{};
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
        result = ConstantScalarRuntimeValue(node);
        break;
    case RenderMaterialGraphNodeKind::ConstantVector2:
        result = ConstantVector2RuntimeValue(node);
        break;
    case RenderMaterialGraphNodeKind::ConstantVector:
        result = ConstantVectorRuntimeValue(node);
        break;
    case RenderMaterialGraphNodeKind::ConstantColor:
        result = ConstantColorRuntimeValue(node);
        break;
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterVector:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ParameterTexture:
        result = GraphParameterRuntimeValue(node, materialAsset);
        break;
    case RenderMaterialGraphNodeKind::TextureSample:
        result = EvaluateTextureSampleOutput(materialAsset, node, outputPin);
        break;
    case RenderMaterialGraphNodeKind::Add: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            lhs.value[0] + rhs.value[0],
            lhs.value[1] + rhs.value[1],
            lhs.value[2] + rhs.value[2],
            lhs.value[3] + rhs.value[3],
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(lhs.textureAssetId, rhs.textureAssetId);
        result.authored = lhs.authored || rhs.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Subtract: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            lhs.value[0] - rhs.value[0],
            lhs.value[1] - rhs.value[1],
            lhs.value[2] - rhs.value[2],
            lhs.value[3] - rhs.value[3],
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(lhs.textureAssetId, rhs.textureAssetId);
        result.authored = lhs.authored || rhs.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Multiply: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(1.0F, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(1.0F, false), stack);
        result = RuntimeValue(
            lhs.value[0] * rhs.value[0],
            lhs.value[1] * rhs.value[1],
            lhs.value[2] * rhs.value[2],
            lhs.value[3] * rhs.value[3],
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(lhs.textureAssetId, rhs.textureAssetId);
        result.authored = lhs.authored || rhs.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Divide: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(1.0F, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(1.0F, false), stack);
        const auto safeDivide = [](float numerator, float denominator) noexcept {
            const float safe = std::max(std::abs(denominator), 0.0001F);
            return numerator / safe;
        };
        result = RuntimeValue(
            safeDivide(lhs.value[0], rhs.value[0]),
            safeDivide(lhs.value[1], rhs.value[1]),
            safeDivide(lhs.value[2], rhs.value[2]),
            safeDivide(lhs.value[3], rhs.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(lhs.textureAssetId, rhs.textureAssetId);
        result.authored = lhs.authored || rhs.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Power: {
        const MaterialGraphRuntimeValue base = EvaluateGraphInput(materialAsset, node, "base", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue exponent = EvaluateGraphInput(materialAsset, node, "exponent", RuntimeValue(1.0F, false), stack);
        const auto power = [](float baseValue, float exponentValue) noexcept {
            return static_cast<float>(std::pow(std::max(baseValue, 0.0F), exponentValue));
        };
        result = RuntimeValue(
            power(base.value[0], exponent.value[0]),
            power(base.value[1], exponent.value[1]),
            power(base.value[2], exponent.value[2]),
            power(base.value[3], exponent.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(base.textureAssetId, exponent.textureAssetId);
        result.authored = base.authored || exponent.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::OneMinus: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            1.0F - input.value[0],
            1.0F - input.value[1],
            1.0F - input.value[2],
            1.0F - input.value[3],
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Absolute: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            std::abs(input.value[0]),
            std::abs(input.value[1]),
            std::abs(input.value[2]),
            std::abs(input.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Minimum: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            std::min(lhs.value[0], rhs.value[0]),
            std::min(lhs.value[1], rhs.value[1]),
            std::min(lhs.value[2], rhs.value[2]),
            std::min(lhs.value[3], rhs.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(lhs.textureAssetId, rhs.textureAssetId);
        result.authored = lhs.authored || rhs.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Maximum: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            std::max(lhs.value[0], rhs.value[0]),
            std::max(lhs.value[1], rhs.value[1]),
            std::max(lhs.value[2], rhs.value[2]),
            std::max(lhs.value[3], rhs.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(lhs.textureAssetId, rhs.textureAssetId);
        result.authored = lhs.authored || rhs.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Saturate: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            Clamp01(input.value[0]),
            Clamp01(input.value[1]),
            Clamp01(input.value[2]),
            Clamp01(input.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Floor: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            std::floor(input.value[0]),
            std::floor(input.value[1]),
            std::floor(input.value[2]),
            std::floor(input.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Ceil: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            std::ceil(input.value[0]),
            std::ceil(input.value[1]),
            std::ceil(input.value[2]),
            std::ceil(input.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Fraction: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        const auto fraction = [](float value) noexcept {
            return value - std::floor(value);
        };
        result = RuntimeValue(
            fraction(input.value[0]),
            fraction(input.value[1]),
            fraction(input.value[2]),
            fraction(input.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::SquareRoot: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        const auto squareRoot = [](float value) noexcept {
            return static_cast<float>(std::sqrt(std::max(value, 0.0F)));
        };
        result = RuntimeValue(
            squareRoot(input.value[0]),
            squareRoot(input.value[1]),
            squareRoot(input.value[2]),
            squareRoot(input.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Sine: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            std::sin(input.value[0]),
            std::sin(input.value[1]),
            std::sin(input.value[2]),
            std::sin(input.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Cosine: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(
            std::cos(input.value[0]),
            std::cos(input.value[1]),
            std::cos(input.value[2]),
            std::cos(input.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::DotProduct: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(0.0F, 0.0F, 0.0F, 1.0F, RenderMaterialGraphPinType::Float3, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(0.0F, 0.0F, 1.0F, 1.0F, RenderMaterialGraphPinType::Float3, false), stack);
        result = RuntimeValue(Dot3(lhs, rhs));
        result.textureAssetId = MergeTextureProvenance(lhs.textureAssetId, rhs.textureAssetId);
        result.authored = lhs.authored || rhs.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::CrossProduct: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(1.0F, 0.0F, 0.0F, 1.0F, RenderMaterialGraphPinType::Float3, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(0.0F, 1.0F, 0.0F, 1.0F, RenderMaterialGraphPinType::Float3, false), stack);
        result = RuntimeValue(
            (lhs.value[1] * rhs.value[2]) - (lhs.value[2] * rhs.value[1]),
            (lhs.value[2] * rhs.value[0]) - (lhs.value[0] * rhs.value[2]),
            (lhs.value[0] * rhs.value[1]) - (lhs.value[1] * rhs.value[0]),
            1.0F,
            RenderMaterialGraphPinType::Float3);
        result.textureAssetId = MergeTextureProvenance(lhs.textureAssetId, rhs.textureAssetId);
        result.authored = lhs.authored || rhs.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Normalize: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, 0.0F, 1.0F, 1.0F, RenderMaterialGraphPinType::Float3, false), stack);
        const float length = Length3(input);
        if (length > 0.0001F) {
            result = RuntimeValue(input.value[0] / length, input.value[1] / length, input.value[2] / length, 1.0F, RenderMaterialGraphPinType::Float3);
        } else {
            result = RuntimeValue(0.0F, 0.0F, 1.0F, 1.0F, RenderMaterialGraphPinType::Float3, false);
        }
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Length: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, 0.0F, 0.0F, 1.0F, RenderMaterialGraphPinType::Float3, false), stack);
        result = RuntimeValue(Length3(input));
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Distance: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(0.0F, 0.0F, 0.0F, 1.0F, RenderMaterialGraphPinType::Float3, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(0.0F, 0.0F, 0.0F, 1.0F, RenderMaterialGraphPinType::Float3, false), stack);
        const MaterialGraphRuntimeValue delta = RuntimeValue(
            lhs.value[0] - rhs.value[0],
            lhs.value[1] - rhs.value[1],
            lhs.value[2] - rhs.value[2],
            1.0F,
            RenderMaterialGraphPinType::Float3);
        result = RuntimeValue(Length3(delta));
        result.textureAssetId = MergeTextureProvenance(lhs.textureAssetId, rhs.textureAssetId);
        result.authored = lhs.authored || rhs.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::BreakVector: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, 0.0F, 0.0F, 1.0F, RenderMaterialGraphPinType::Float4, false), stack);
        float channel = 0.0F;
        if (outputPin == "x") {
            channel = input.value[0];
        } else if (outputPin == "y") {
            channel = input.value[1];
        } else if (outputPin == "z") {
            channel = input.value[2];
        } else if (outputPin == "w") {
            channel = input.value[3];
        }
        result = RuntimeValue(channel);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::MakeVector: {
        const MaterialGraphRuntimeValue x = EvaluateGraphInput(materialAsset, node, "x", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue y = EvaluateGraphInput(materialAsset, node, "y", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue z = EvaluateGraphInput(materialAsset, node, "z", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue w = EvaluateGraphInput(materialAsset, node, "w", RuntimeValue(1.0F, false), stack);
        result = RuntimeValue(x.value[0], y.value[0], z.value[0], w.value[0], RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(MergeTextureProvenance(x.textureAssetId, y.textureAssetId), MergeTextureProvenance(z.textureAssetId, w.textureAssetId));
        result.authored = x.authored || y.authored || z.authored || w.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Step: {
        const MaterialGraphRuntimeValue edge = EvaluateGraphInput(materialAsset, node, "edge", RuntimeValue(0.5F, false), stack);
        const MaterialGraphRuntimeValue value = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        const auto step = [](float edgeValue, float inputValue) noexcept {
            return inputValue < edgeValue ? 0.0F : 1.0F;
        };
        result = RuntimeValue(
            step(edge.value[0], value.value[0]),
            step(edge.value[1], value.value[1]),
            step(edge.value[2], value.value[2]),
            step(edge.value[3], value.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(edge.textureAssetId, value.textureAssetId);
        result.authored = edge.authored || value.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::SmoothStep: {
        const MaterialGraphRuntimeValue minValue = EvaluateGraphInput(materialAsset, node, "min", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue maxValue = EvaluateGraphInput(materialAsset, node, "max", RuntimeValue(1.0F, false), stack);
        const MaterialGraphRuntimeValue value = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        const auto smoothStep = [](float minInput, float maxInput, float inputValue) noexcept {
            const float range = maxInput - minInput;
            const float safeRange = std::abs(range) > 0.0001F ? range : 0.0001F;
            const float t = Clamp01((inputValue - minInput) / safeRange);
            return t * t * (3.0F - (2.0F * t));
        };
        result = RuntimeValue(
            smoothStep(minValue.value[0], maxValue.value[0], value.value[0]),
            smoothStep(minValue.value[1], maxValue.value[1], value.value[1]),
            smoothStep(minValue.value[2], maxValue.value[2], value.value[2]),
            smoothStep(minValue.value[3], maxValue.value[3], value.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(MergeTextureProvenance(minValue.textureAssetId, maxValue.textureAssetId), value.textureAssetId);
        result.authored = minValue.authored || maxValue.authored || value.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::If: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue less = EvaluateGraphInput(materialAsset, node, "less", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue equal = EvaluateGraphInput(materialAsset, node, "equal", RuntimeValue(0.5F, false), stack);
        const MaterialGraphRuntimeValue greater = EvaluateGraphInput(materialAsset, node, "greater", RuntimeValue(1.0F, false), stack);
        const MaterialGraphRuntimeValue& selected =
            lhs.value[0] > rhs.value[0] ? greater :
            (std::abs(lhs.value[0] - rhs.value[0]) <= 0.0001F ? equal : less);
        result = RuntimeValue(selected.value[0], selected.value[1], selected.value[2], selected.value[3], RenderMaterialGraphPinType::Float4);
        result.textureAssetId = selected.textureAssetId;
        result.authored = lhs.authored || rhs.authored || selected.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::RuntimeSwitch: {
        const MaterialGraphRuntimeValue index = EvaluateGraphInput(materialAsset, node, "index", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue defaultValue = EvaluateGraphInput(materialAsset, node, "default", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue case0 = EvaluateGraphInput(materialAsset, node, "case0", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue case1 = EvaluateGraphInput(materialAsset, node, "case1", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue case2 = EvaluateGraphInput(materialAsset, node, "case2", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue case3 = EvaluateGraphInput(materialAsset, node, "case3", RuntimeValue(0.0F, false), stack);
        const int selectedIndex = static_cast<int>(std::floor(index.value[0] + 0.5F));
        const MaterialGraphRuntimeValue* selected = &defaultValue;
        if (selectedIndex == 0) {
            selected = &case0;
        } else if (selectedIndex == 1) {
            selected = &case1;
        } else if (selectedIndex == 2) {
            selected = &case2;
        } else if (selectedIndex == 3) {
            selected = &case3;
        }
        result = RuntimeValue(selected->value[0], selected->value[1], selected->value[2], selected->value[3], RenderMaterialGraphPinType::Float4);
        result.textureAssetId = selected->textureAssetId;
        result.authored = index.authored || selected->authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Desaturate: {
        const MaterialGraphRuntimeValue color = EvaluateGraphInput(materialAsset, node, "color", RuntimeValue(1.0F, 1.0F, 1.0F, 1.0F, RenderMaterialGraphPinType::Color, false), stack);
        const MaterialGraphRuntimeValue fraction = EvaluateGraphInput(materialAsset, node, "fraction", RuntimeValue(1.0F, false), stack);
        const float amount = Clamp01(fraction.value[0]);
        const float luma = (color.value[0] * 0.299F) + (color.value[1] * 0.587F) + (color.value[2] * 0.114F);
        const auto blend = [luma, amount](float channel) noexcept {
            return channel + ((luma - channel) * amount);
        };
        result = RuntimeValue(blend(color.value[0]), blend(color.value[1]), blend(color.value[2]), color.value[3], RenderMaterialGraphPinType::Color);
        result.textureAssetId = color.textureAssetId;
        result.authored = color.authored || fraction.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Fresnel: {
        const MaterialGraphRuntimeValue normal = EvaluateGraphInput(materialAsset, node, "normal", RuntimeValue(0.0F, 0.0F, 1.0F, 1.0F, RenderMaterialGraphPinType::Float3, false), stack);
        const MaterialGraphRuntimeValue view = EvaluateGraphInput(materialAsset, node, "view", RuntimeValue(0.0F, 0.0F, 1.0F, 1.0F, RenderMaterialGraphPinType::Float3, false), stack);
        const MaterialGraphRuntimeValue exponent = EvaluateGraphInput(materialAsset, node, "exponent", RuntimeValue(5.0F, false), stack);
        const MaterialGraphRuntimeValue base = EvaluateGraphInput(materialAsset, node, "base", RuntimeValue(0.0F, false), stack);
        const float normalLength = std::max(Length3(normal), 0.0001F);
        const float viewLength = std::max(Length3(view), 0.0001F);
        const float facing = Clamp01(
            ((normal.value[0] / normalLength) * (view.value[0] / viewLength)) +
            ((normal.value[1] / normalLength) * (view.value[1] / viewLength)) +
            ((normal.value[2] / normalLength) * (view.value[2] / viewLength)));
        const float edge = static_cast<float>(std::pow(1.0F - facing, std::max(exponent.value[0], 0.0001F)));
        const float baseReflect = Clamp01(base.value[0]);
        result = RuntimeValue(edge + ((1.0F - edge) * baseReflect));
        result.textureAssetId = MergeTextureProvenance(normal.textureAssetId, view.textureAssetId);
        result.authored = normal.authored || view.authored || exponent.authored || base.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Negate: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(-input.value[0], -input.value[1], -input.value[2], -input.value[3], RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Sign: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        const auto sign = [](float value) noexcept {
            return value > 0.0F ? 1.0F : (value < 0.0F ? -1.0F : 0.0F);
        };
        result = RuntimeValue(sign(input.value[0]), sign(input.value[1]), sign(input.value[2]), sign(input.value[3]), RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Round: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(std::round(input.value[0]), std::round(input.value[1]), std::round(input.value[2]), std::round(input.value[3]), RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Truncate: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(std::trunc(input.value[0]), std::trunc(input.value[1]), std::trunc(input.value[2]), std::trunc(input.value[3]), RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Tangent: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(std::tan(input.value[0]), std::tan(input.value[1]), std::tan(input.value[2]), std::tan(input.value[3]), RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::ArcSine: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        const auto arcSine = [](float value) noexcept {
            return static_cast<float>(std::asin(std::clamp(value, -1.0F, 1.0F)));
        };
        result = RuntimeValue(arcSine(input.value[0]), arcSine(input.value[1]), arcSine(input.value[2]), arcSine(input.value[3]), RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::ArcCosine: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(1.0F, false), stack);
        const auto arcCosine = [](float value) noexcept {
            return static_cast<float>(std::acos(std::clamp(value, -1.0F, 1.0F)));
        };
        result = RuntimeValue(arcCosine(input.value[0]), arcCosine(input.value[1]), arcCosine(input.value[2]), arcCosine(input.value[3]), RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::ArcTangent: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(std::atan(input.value[0]), std::atan(input.value[1]), std::atan(input.value[2]), std::atan(input.value[3]), RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::ArcTangent2: {
        const MaterialGraphRuntimeValue y = EvaluateGraphInput(materialAsset, node, "y", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue x = EvaluateGraphInput(materialAsset, node, "x", RuntimeValue(1.0F, false), stack);
        result = RuntimeValue(
            std::atan2(y.value[0], x.value[0]),
            std::atan2(y.value[1], x.value[1]),
            std::atan2(y.value[2], x.value[2]),
            std::atan2(y.value[3], x.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(y.textureAssetId, x.textureAssetId);
        result.authored = y.authored || x.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::ArcSineFast: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(FastAsinScalar(input.value[0]), FastAsinScalar(input.value[1]), FastAsinScalar(input.value[2]), FastAsinScalar(input.value[3]), RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::ArcCosineFast: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(1.0F, false), stack);
        result = RuntimeValue(FastAcosScalar(input.value[0]), FastAcosScalar(input.value[1]), FastAcosScalar(input.value[2]), FastAcosScalar(input.value[3]), RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::ArcTangentFast: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        result = RuntimeValue(FastAtanScalar(input.value[0]), FastAtanScalar(input.value[1]), FastAtanScalar(input.value[2]), FastAtanScalar(input.value[3]), RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::ArcTangent2Fast: {
        const MaterialGraphRuntimeValue y = EvaluateGraphInput(materialAsset, node, "y", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue x = EvaluateGraphInput(materialAsset, node, "x", RuntimeValue(1.0F, false), stack);
        result = RuntimeValue(
            FastAtan2Scalar(y.value[0], x.value[0]),
            FastAtan2Scalar(y.value[1], x.value[1]),
            FastAtan2Scalar(y.value[2], x.value[2]),
            FastAtan2Scalar(y.value[3], x.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(y.textureAssetId, x.textureAssetId);
        result.authored = y.authored || x.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Clamp: {
        const MaterialGraphRuntimeValue input = EvaluateGraphInput(materialAsset, node, "value", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue minValue = EvaluateGraphInput(materialAsset, node, "min", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue maxValue = EvaluateGraphInput(materialAsset, node, "max", RuntimeValue(1.0F, false), stack);
        result = RuntimeValue(
            std::clamp(input.value[0], minValue.value[0], maxValue.value[0]),
            std::clamp(input.value[1], minValue.value[1], maxValue.value[1]),
            std::clamp(input.value[2], minValue.value[2], maxValue.value[2]),
            std::clamp(input.value[3], minValue.value[3], maxValue.value[3]),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = input.textureAssetId;
        result.authored = input.authored || minValue.authored || maxValue.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Lerp: {
        const MaterialGraphRuntimeValue lhs = EvaluateGraphInput(materialAsset, node, "a", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue rhs = EvaluateGraphInput(materialAsset, node, "b", RuntimeValue(1.0F, false), stack);
        const MaterialGraphRuntimeValue t = EvaluateGraphInput(materialAsset, node, "t", RuntimeValue(0.0F, false), stack);
        const float amount = Clamp01(t.value[0]);
        result = RuntimeValue(
            lhs.value[0] + ((rhs.value[0] - lhs.value[0]) * amount),
            lhs.value[1] + ((rhs.value[1] - lhs.value[1]) * amount),
            lhs.value[2] + ((rhs.value[2] - lhs.value[2]) * amount),
            lhs.value[3] + ((rhs.value[3] - lhs.value[3]) * amount),
            RenderMaterialGraphPinType::Float4);
        result.textureAssetId = MergeTextureProvenance(lhs.textureAssetId, rhs.textureAssetId);
        result.authored = lhs.authored || rhs.authored || t.authored;
        break;
    }
    case RenderMaterialGraphNodeKind::Sobol: {
        const MaterialGraphRuntimeValue cell = EvaluateGraphInput(materialAsset, node, "cell", RuntimeValue(0.0F, 0.0F, 0.0F, 1.0F, RenderMaterialGraphPinType::Float2, false), stack);
        const MaterialGraphRuntimeValue index = EvaluateGraphInput(materialAsset, node, "index", RuntimeValue(0.0F, false), stack);
        const MaterialGraphRuntimeValue seed = EvaluateGraphInput(materialAsset, node, "seed", RuntimeValue(0.0F, 0.0F, 0.0F, 1.0F, RenderMaterialGraphPinType::Float2, false), stack);
        result = SobolRuntimeValue(cell, index, seed);
        result.textureAssetId = MergeTextureProvenance(MergeTextureProvenance(cell.textureAssetId, index.textureAssetId), seed.textureAssetId);
        break;
    }
    case RenderMaterialGraphNodeKind::NormalUnpack:
        result = EvaluateGraphInput(materialAsset, node, "color", RuntimeValue(0.5F, 0.5F, 1.0F, 1.0F, RenderMaterialGraphPinType::Color, false), stack);
        result.type = RenderMaterialGraphPinType::Normal;
        break;
    case RenderMaterialGraphNodeKind::Uv:
        result = RuntimeValue(0.0F, 0.0F, 0.0F, 0.0F, RenderMaterialGraphPinType::Float2, false);
        break;
    case RenderMaterialGraphNodeKind::MaterialOutput:
        break;
    }

    stack.pop_back();
    return result;
}

[[nodiscard]] std::optional<MaterialGraphRuntimeValue> EvaluateMaterialOutputInput(
    const RenderMaterialAssetData& materialAsset,
    const RenderMaterialGraphNode& output,
    std::string_view pin) {
    const RenderMaterialGraphLink* link = FindGraphInputLink(materialAsset.graph, output.id, pin);
    if (link == nullptr) {
        return std::nullopt;
    }
    const RenderMaterialGraphNode* source = FindRenderMaterialGraphNode(materialAsset.graph, link->fromNodeId);
    if (source == nullptr) {
        return std::nullopt;
    }
    std::vector<std::uint32_t> stack;
    return EvaluateGraphNodeOutput(materialAsset, *source, link->fromPin, stack);
}

void ApplyMaterialOutputGraphToPbrDesc(RenderMaterialDesc& desc, const RenderMaterialAssetData& materialAsset) {
    if (!HasGraphAuthoringData(materialAsset.graph)) {
        return;
    }

    const RenderMaterialGraphNode* output = nullptr;
    for (const RenderMaterialGraphNode& node : materialAsset.graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            output = &node;
            break;
        }
    }
    if (output == nullptr) {
        return;
    }

    const MaterialSurface surfaceDefaults;
    const std::optional<MaterialGraphRuntimeValue> baseColor = EvaluateMaterialOutputInput(materialAsset, *output, "baseColor");
    if (!baseColor.has_value()) {
        desc.baseColor[0] = surfaceDefaults.baseColor[0];
        desc.baseColor[1] = surfaceDefaults.baseColor[1];
        desc.baseColor[2] = surfaceDefaults.baseColor[2];
        desc.baseColor[3] = surfaceDefaults.baseColor[3];
        desc.albedoTextureAssetId = 0U;
    } else if (baseColor->authored) {
        desc.baseColor[0] = Clamp01(baseColor->value[0]);
        desc.baseColor[1] = Clamp01(baseColor->value[1]);
        desc.baseColor[2] = Clamp01(baseColor->value[2]);
        desc.baseColor[3] = Clamp01(baseColor->value[3]);
        desc.albedoTextureAssetId = baseColor->textureAssetId;
    }

    if (const std::optional<MaterialGraphRuntimeValue> normal = EvaluateMaterialOutputInput(materialAsset, *output, "normal");
        normal.has_value() && normal->authored && normal->textureAssetId != 0U) {
        desc.normalTextureAssetId = normal->textureAssetId;
        WriteRendererMaterialGraphDebugLog("runtime", "pbr-fallback-normal-from-graph textureAssetId=" + std::to_string(normal->textureAssetId));
    } else {
        WriteRendererMaterialGraphDebugLog("runtime", "pbr-fallback-normal-missing-or-unresolved");
    }
    if (const std::optional<MaterialGraphRuntimeValue> roughness = EvaluateMaterialOutputInput(materialAsset, *output, "roughness")) {
        if (roughness->authored) {
            desc.roughnessFactor = Clamp01(roughness->value[0]);
            if (roughness->textureAssetId != 0U) {
                desc.metallicRoughnessTextureAssetId = roughness->textureAssetId;
            }
        }
    } else {
        desc.roughnessFactor = surfaceDefaults.roughness;
    }
    if (const std::optional<MaterialGraphRuntimeValue> metallic = EvaluateMaterialOutputInput(materialAsset, *output, "metallic")) {
        if (metallic->authored) {
            desc.metallicFactor = Clamp01(metallic->value[0]);
            if (metallic->textureAssetId != 0U) {
                desc.metallicRoughnessTextureAssetId = metallic->textureAssetId;
            }
        }
    } else {
        desc.metallicFactor = surfaceDefaults.metallic;
    }
    if (const std::optional<MaterialGraphRuntimeValue> occlusion = EvaluateMaterialOutputInput(materialAsset, *output, "occlusion")) {
        if (occlusion->authored) {
            desc.occlusionStrength = Clamp01(occlusion->value[0]);
            if (occlusion->textureAssetId != 0U) {
                desc.occlusionTextureAssetId = occlusion->textureAssetId;
            }
        }
    } else {
        desc.occlusionStrength = surfaceDefaults.occlusion;
    }
    if (const std::optional<MaterialGraphRuntimeValue> emissive = EvaluateMaterialOutputInput(materialAsset, *output, "emissive")) {
        if (emissive->authored) {
            desc.emissiveColor[0] = std::max(emissive->value[0], 0.0F);
            desc.emissiveColor[1] = std::max(emissive->value[1], 0.0F);
            desc.emissiveColor[2] = std::max(emissive->value[2], 0.0F);
            if (emissive->textureAssetId != 0U) {
                desc.emissiveTextureAssetId = emissive->textureAssetId;
            }
        }
    } else {
        desc.emissiveColor[0] = surfaceDefaults.emissive[0];
        desc.emissiveColor[1] = surfaceDefaults.emissive[1];
        desc.emissiveColor[2] = surfaceDefaults.emissive[2];
    }
    if (const std::optional<MaterialGraphRuntimeValue> alpha = EvaluateMaterialOutputInput(materialAsset, *output, "alpha");
        alpha.has_value() && alpha->authored) {
        desc.baseColor[3] = Clamp01(alpha->value[0]);
    }
    if (const std::optional<MaterialGraphRuntimeValue> alphaClipThreshold = EvaluateMaterialOutputInput(materialAsset, *output, "alphaClipThreshold")) {
        if (alphaClipThreshold->authored) {
            desc.alphaCutoff = Clamp01(alphaClipThreshold->value[0]);
        }
    } else {
        desc.alphaCutoff = surfaceDefaults.alphaClipThreshold;
    }
}

} // namespace

RuntimeMaterialResolver::RuntimeMaterialResolver(RenderMaterialGraphBuildContext graphBuildContext) noexcept
    : graphBuildContext_(std::move(graphBuildContext)) {}

void RuntimeMaterialResolver::SetGraphBuildContext(RenderMaterialGraphBuildContext graphBuildContext) noexcept {
    graphBuildContext_ = std::move(graphBuildContext);
}

std::uint64_t RuntimeMaterialResolver::EmbeddedMaterialAssetId(std::uint64_t meshAssetId, std::uint32_t slotIndex, std::string_view materialName) noexcept {
    std::string key = "RenderMeshEmbeddedMaterial:";
    key += std::to_string(meshAssetId);
    key += ':';
    key += std::to_string(slotIndex);
    key += ':';
    key += materialName;
    return kb::assets::MakeAssetId(key).value;
}

std::uint64_t RuntimeMaterialResolver::MaterialRuntimeContentHash(
    kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata) {
    std::unordered_set<std::uint64_t> visited;
    return MaterialRuntimeContentHashImpl(manager, metadata, visited);
}

RuntimeFallbackMaterialProfile RuntimeMaterialResolver::FallbackMaterialProfile(RuntimeFallbackMaterialKind kind) noexcept {
    RenderMaterialDesc desc{};
    switch (kind) {
    case RuntimeFallbackMaterialKind::Default:
        return RuntimeFallbackMaterialProfile{
            .kind = RuntimeFallbackMaterialKind::Default,
            .status = RuntimeMaterialResolveStatus::DefaultMaterial,
            .stableName = "runtime.default_material",
            .desc = desc,
        };
    case RuntimeFallbackMaterialKind::Error:
        desc.baseColor[0] = 1.0F;
        desc.baseColor[1] = 0.0F;
        desc.baseColor[2] = 1.0F;
        desc.baseColor[3] = 1.0F;
        desc.roughnessFactor = 0.65F;
        return RuntimeFallbackMaterialProfile{
            .kind = RuntimeFallbackMaterialKind::Error,
            .status = RuntimeMaterialResolveStatus::ErrorMaterial,
            .stableName = "runtime.error_material",
            .desc = desc,
        };
    }
    return FallbackMaterialProfile(RuntimeFallbackMaterialKind::Error);
}

std::string_view RuntimeMaterialRenderModeName(RuntimeMaterialRenderMode mode) noexcept {
    switch (mode) {
    case RuntimeMaterialRenderMode::BuiltinPbr: return "BuiltinPbr";
    case RuntimeMaterialRenderMode::CpuPbrFlatteningFallback: return "CpuPbrFlatteningFallback";
    case RuntimeMaterialRenderMode::GpuMaterialGraph: return "GpuMaterialGraph";
    }
    return "BuiltinPbr";
}

std::string_view RuntimeMaterialCpuFallbackReasonName(RuntimeMaterialCpuFallbackReason reason) noexcept {
    switch (reason) {
    case RuntimeMaterialCpuFallbackReason::None: return "None";
    case RuntimeMaterialCpuFallbackReason::GraphProgramUnavailable: return "GraphProgramUnavailable";
    }
    return "None";
}

RenderMaterialDesc RuntimeMaterialResolver::DefaultMaterialDesc() noexcept {
    return FallbackMaterialProfile(RuntimeFallbackMaterialKind::Default).desc;
}

RenderMaterialDesc RuntimeMaterialResolver::ErrorMaterialDesc() noexcept {
    return FallbackMaterialProfile(RuntimeFallbackMaterialKind::Error).desc;
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
    const RenderMaterialAssetData& inlineMaterialAsset) const {
    ResolvedRuntimeMaterialDesc resolved{};
    const RuntimeMaterialSourceGraphLoadResult authoritativeGraph =
        LoadRuntimeMaterialSourceGraph(manager, inlineMaterialAsset);
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : authoritativeGraph.parseResult.diagnostics) {
        resolved.graphDiagnostics.push_back(RenderMaterialGraphDiagnostic{
            .severity = diagnostic.severity == RenderMaterialAssetParseDiagnosticSeverity::Warning
                ? RenderMaterialGraphDiagnosticSeverity::Warning
                : RenderMaterialGraphDiagnosticSeverity::Error,
            .kind = RenderMaterialGraphDiagnosticKind::SourceGraphLoadDiagnostic,
            .assetId = authoritativeGraph.assetId.IsValid() ? authoritativeGraph.assetId.value : materialMetadata.id.value,
            .sourcePath = authoritativeGraph.path.generic_string(),
            .message = RuntimeMaterialParseDiagnosticMessage(diagnostic),
        });
    }
    if (!authoritativeGraph.graph.has_value()) {
        resolved.desc = inlineMaterialAsset.desc;
        resolved.graphDiagnostics.push_back(RenderMaterialGraphDiagnostic{
            .severity = RenderMaterialGraphDiagnosticSeverity::Error,
            .kind = RenderMaterialGraphDiagnosticKind::MissingSourceGraph,
            .assetId = materialMetadata.id.value,
            .sourcePath = inlineMaterialAsset.graphSourceAssetPath,
            .message = "The authoritative sourceGraph asset is missing or could not be loaded; the stale inline graph was not compiled.",
        });
        return resolved;
    }
    RenderMaterialAssetData materialAsset = inlineMaterialAsset;
    materialAsset.graph = *authoritativeGraph.graph;
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
    ApplyGraphParameterValuesToPbrDesc(resolved.desc, materialAsset);
    ApplyGraphTextureSlotValuesToPbrDesc(resolved.desc, materialAsset);
    ApplyMaterialOutputGraphToPbrDesc(resolved.desc, materialAsset);

    if (HasGraphAuthoringData(materialAsset.graph)) {
        WriteRendererMaterialGraphDebugLog(
            "runtime",
            "resolve-graph asset=" + std::to_string(materialMetadata.id.value) +
                " nodes=" + std::to_string(materialAsset.graph.nodes.size()) +
                " links=" + std::to_string(materialAsset.graph.links.size()) +
                " values=" + std::to_string(materialAsset.graphParameterValues.size()));
        RuntimeMaterialFunctionLibraryBuildResult functionLibrary = BuildRuntimeMaterialFunctionLibrary(manager, materialAsset.graph);
        resolved.graphDiagnostics.insert(
            resolved.graphDiagnostics.end(),
            std::make_move_iterator(functionLibrary.diagnostics.begin()),
            std::make_move_iterator(functionLibrary.diagnostics.end()));
        std::vector<RenderMaterialGraphDiagnostic> collectionDiagnostics =
            LoadRuntimeMaterialParameterCollectionDefaults(manager, materialAsset.graph);
        const bool collectionHasError = std::any_of(collectionDiagnostics.begin(), collectionDiagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
            return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
        });
        resolved.graphDiagnostics.insert(
            resolved.graphDiagnostics.end(),
            collectionDiagnostics.begin(),
            collectionDiagnostics.end());
        if (collectionHasError) {
            return resolved;
        }
        RenderMaterialGraphBuildContext graphContext = graphBuildContext_;
        graphContext.assetId = materialMetadata.id.value;
        graphContext.sourcePath = materialMetadata.virtualPath.generic_string();
        graphContext.functionLibrary = &functionLibrary.library;
        const RenderMaterialGraphCompileResult graphCompile = CompileRenderMaterialGraphToShaderSource(
            materialAsset.graph,
            graphContext);
        resolved.graphDiagnostics.insert(
            resolved.graphDiagnostics.end(),
            graphCompile.diagnostics.begin(),
            graphCompile.diagnostics.end());
        if (graphCompile.Succeeded()) {
            WriteRendererMaterialGraphDebugLog(
                "runtime",
                "resolve-graph-compile-ok asset=" + std::to_string(materialMetadata.id.value) +
                    " sourceHash=" + std::to_string(graphCompile.shader.sourceHash));
            std::uint64_t materialTypeId = 1469598103934665603ULL;
            for (const char ch : materialAsset.materialType) {
                materialTypeId ^= static_cast<unsigned char>(ch);
                materialTypeId *= 1099511628211ULL;
            }
            RenderMaterialGraphProgramBindingResult bindingResult = BuildRenderMaterialGraphProgramBinding(
                materialTypeId,
                materialAsset.materialTypeVersion == 0U ? 1U : materialAsset.materialTypeVersion,
                graphCompile.shader,
                materialAsset.graphParameterValues);
            const bool bindingHasError = std::any_of(bindingResult.diagnostics.begin(), bindingResult.diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
                return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
            });
            resolved.graphDiagnostics.insert(
                resolved.graphDiagnostics.end(),
                bindingResult.diagnostics.begin(),
                bindingResult.diagnostics.end());
            if (!bindingHasError) {
                resolved.graphProgram = std::move(bindingResult.binding);
                WriteRendererMaterialGraphDebugLog(
                    "runtime",
                    "resolve-graph-binding-ok asset=" + std::to_string(materialMetadata.id.value) +
                        " textures=" + std::to_string(resolved.graphProgram.textures.size()) +
                        " uniforms=" + std::to_string(resolved.graphProgram.uniforms.size()));
            } else {
                WriteRendererMaterialGraphDebugLog(
                    "runtime",
                    "resolve-graph-binding-error asset=" + std::to_string(materialMetadata.id.value) +
                        " diagnostics=" + std::to_string(bindingResult.diagnostics.size()));
            }
        } else {
            WriteRendererMaterialGraphDebugLog(
                "runtime",
                "resolve-graph-compile-failed asset=" + std::to_string(materialMetadata.id.value) +
                    " diagnostics=" + std::to_string(graphCompile.diagnostics.size()));
        }
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
        const std::uint64_t runtimeContentHash = MaterialRuntimeContentHash(manager, metadata);
        const std::filesystem::path path = ResolveAssetPhysicalPath(manager, metadata);
        RenderMaterialAssetParseResult loaded = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(path, metadata.id);
        if (!loaded.asset.has_value()) {
            ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
                RuntimeMaterialResolveStatus::ErrorMaterial,
                RuntimeMaterialResolveDiagnosticKind::MaterialLoadFailed,
                RuntimeMaterialResolveDiagnosticSeverity::Error,
                metadata.id,
                path,
                "Material asset could not be loaded; using the error material.");
            fallback.contentHash = runtimeContentHash;
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

        const RuntimeMaterialSourceGraphLoadResult authoritativeGraph =
            LoadRuntimeMaterialSourceGraph(manager, *loaded.asset);
        if (!authoritativeGraph.graph.has_value()) {
            ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
                RuntimeMaterialResolveStatus::ErrorMaterial,
                RuntimeMaterialResolveDiagnosticKind::MaterialGraphValidationFailed,
                RuntimeMaterialResolveDiagnosticSeverity::Error,
                metadata.id,
                path,
                "The authoritative sourceGraph asset is missing or invalid; the stale inline graph was not used.");
            fallback.contentHash = runtimeContentHash;
            fallback.failurePolicy = loaded.asset->graph.artifactFailurePolicy;
            AppendParseDiagnostics(
                fallback,
                authoritativeGraph.parseResult,
                authoritativeGraph.assetId.IsValid() ? authoritativeGraph.assetId : metadata.id);
            return fallback;
        }
        loaded.asset->graph = *authoritativeGraph.graph;

        const RenderMaterialTypeReferenceValidationResult typeReferenceValidation =
            ValidateRenderMaterialTypeReference(*loaded.asset, metadata, manager);
        if (!typeReferenceValidation.Succeeded()) {
            ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
                RuntimeMaterialResolveStatus::ErrorMaterial,
                RuntimeMaterialResolveDiagnosticKind::MaterialTypeReferenceValidationFailed,
                RuntimeMaterialResolveDiagnosticSeverity::Error,
                metadata.id,
                path,
                "Material Type reference is invalid; using the error material.");
            fallback.contentHash = runtimeContentHash;
            fallback.failurePolicy = loaded.asset->graph.artifactFailurePolicy;
            fallback.diagnostics.clear();
            AppendParseDiagnostics(fallback, loaded, metadata.id);
            AppendParseDiagnostics(
                fallback,
                authoritativeGraph.parseResult,
                authoritativeGraph.assetId.IsValid() ? authoritativeGraph.assetId : metadata.id);
            AppendMaterialTypeReferenceDiagnostics(fallback, typeReferenceValidation, metadata.id, path);
            return fallback;
        }

        const std::vector<RenderMaterialGraphDiagnostic> graphDiagnostics = ValidateMaterialOutputRuntimeGraphDiagnostics(*loaded.asset);
        const bool graphHasError = std::any_of(graphDiagnostics.begin(), graphDiagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
            return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
        });
        if (graphHasError) {
            ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
                RuntimeMaterialResolveStatus::ErrorMaterial,
                RuntimeMaterialResolveDiagnosticKind::MaterialGraphValidationFailed,
                RuntimeMaterialResolveDiagnosticSeverity::Error,
                metadata.id,
                path,
                "Material graph validation failed; using the error material.");
            fallback.contentHash = runtimeContentHash;
            fallback.failurePolicy = loaded.asset->graph.artifactFailurePolicy;
            fallback.diagnostics.clear();
            AppendParseDiagnostics(fallback, loaded, metadata.id);
            AppendParseDiagnostics(
                fallback,
                authoritativeGraph.parseResult,
                authoritativeGraph.assetId.IsValid() ? authoritativeGraph.assetId : metadata.id);
            AppendGraphValidationDiagnostics(fallback, graphDiagnostics, metadata.id, path);
            return fallback;
        }

        ResolvedRuntimeMaterialAsset resolved{
            .material = ResolveLoadedMaterial(manager, metadata, *loaded.asset),
            .contentHash = runtimeContentHash,
            .status = RuntimeMaterialResolveStatus::Resolved,
            .renderMode = RuntimeMaterialRenderMode::BuiltinPbr,
            .failurePolicy = loaded.asset->graph.artifactFailurePolicy,
            .resolved = true,
        };
        if (HasGraphAuthoringData(loaded.asset->graph)) {
            if (resolved.material.graphProgram.active) {
                resolved.renderMode = RuntimeMaterialRenderMode::GpuMaterialGraph;
            } else {
                resolved.renderMode = RuntimeMaterialRenderMode::CpuPbrFlatteningFallback;
                resolved.cpuFallbackReason = RuntimeMaterialCpuFallbackReason::GraphProgramUnavailable;
                resolved.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
                    .severity = RuntimeMaterialResolveDiagnosticSeverity::Warning,
                    .kind = RuntimeMaterialResolveDiagnosticKind::MaterialGraphValidationFailed,
                    .assetId = metadata.id,
                    .path = path,
                    .message = "Material graph has no GPU program; falling back to CPU PBR flattening.",
                });
                AppendGraphValidationDiagnostics(resolved, resolved.material.graphDiagnostics, metadata.id, path);
            }
        }
        AppendParseDiagnostics(resolved, loaded, metadata.id);
        AppendParseDiagnostics(
            resolved,
            authoritativeGraph.parseResult,
            authoritativeGraph.assetId.IsValid() ? authoritativeGraph.assetId : metadata.id);
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
        fallback.contentHash = MaterialRuntimeContentHash(manager, metadata);
        return fallback;
    }

    const std::uint64_t runtimeContentHash = MaterialRuntimeContentHash(manager, metadata);
    const kb::assets::AssetHandle<RenderMaterialInstanceAssetData> instance = manager.Load<RenderMaterialInstanceAssetData>(metadata.id);
    if (!instance.IsLoaded() || !instance->parentMaterialAssetId.IsValid()) {
        ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
            RuntimeMaterialResolveStatus::ErrorMaterial,
            RuntimeMaterialResolveDiagnosticKind::MaterialInstanceLoadFailed,
            RuntimeMaterialResolveDiagnosticSeverity::Error,
            metadata.id,
            ResolveAssetPhysicalPath(manager, metadata),
            "Material instance could not be loaded or has no parent; using the error material.");
        fallback.contentHash = runtimeContentHash;
        return fallback;
    }
    const kb::assets::AssetMetadata* parentMetadata = manager.Registry().Find(instance->parentMaterialAssetId);
    if (parentMetadata == nullptr || (parentMetadata->type != "RenderMaterial" && parentMetadata->type != "RenderMaterialInstance")) {
        ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
            RuntimeMaterialResolveStatus::ErrorMaterial,
            RuntimeMaterialResolveDiagnosticKind::MissingParentMaterial,
            RuntimeMaterialResolveDiagnosticSeverity::Error,
            metadata.id,
            ResolveAssetPhysicalPath(manager, metadata),
            "Material instance parent is missing or is not a material/material instance; using the error material.");
        fallback.diagnostics.front().parentAssetId = instance->parentMaterialAssetId;
        fallback.contentHash = runtimeContentHash;
        return fallback;
    }

    std::unordered_set<std::uint64_t> visited;
    visited.insert(metadata.id.value);
    EffectiveRuntimeMaterialDocumentResult parentDocument = LoadEffectiveRuntimeMaterialDocument(manager, *parentMetadata, visited);
    if (!parentDocument.material.has_value()) {
        ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
            RuntimeMaterialResolveStatus::ErrorMaterial,
            parentDocument.diagnostics.empty() ? RuntimeMaterialResolveDiagnosticKind::ParentMaterialLoadFailed : parentDocument.diagnostics.front().kind,
            RuntimeMaterialResolveDiagnosticSeverity::Error,
            metadata.id,
            ResolveAssetPhysicalPath(manager, metadata),
            "Material instance parent could not be resolved; using the error material.");
        fallback.diagnostics = std::move(parentDocument.diagnostics);
        if (fallback.diagnostics.empty()) {
            fallback.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
                .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
                .kind = RuntimeMaterialResolveDiagnosticKind::ParentMaterialLoadFailed,
                .assetId = metadata.id,
                .parentAssetId = parentMetadata->id,
                .path = ResolveAssetPhysicalPath(manager, metadata),
                .message = "Material instance parent could not be resolved; using the error material.",
            });
        }
        for (RuntimeMaterialResolveDiagnostic& diagnostic : fallback.diagnostics) {
            if (!diagnostic.assetId.IsValid()) {
                diagnostic.assetId = metadata.id;
            }
            if (!diagnostic.parentAssetId.IsValid()) {
                diagnostic.parentAssetId = parentMetadata->id;
            }
        }
        fallback.contentHash = runtimeContentHash;
        return fallback;
    }

    const RenderMaterialInstanceValidationResult validation = RenderMaterialInstanceAssetLoader::ValidateAgainstParent(*instance, *parentDocument.material);
    if (!validation.Succeeded()) {
        ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
            RuntimeMaterialResolveStatus::ErrorMaterial,
            RuntimeMaterialResolveDiagnosticKind::MaterialInstanceValidationFailed,
            RuntimeMaterialResolveDiagnosticSeverity::Error,
            metadata.id,
            ResolveAssetPhysicalPath(manager, metadata),
            "Material instance override is not compatible with its parent; using the error material.");
        fallback.diagnostics.clear();
        for (const RenderMaterialInstanceValidationDiagnostic& diagnostic : validation.diagnostics) {
            fallback.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
                .severity = RuntimeMaterialResolveDiagnosticSeverity::Error,
                .kind = RuntimeMaterialResolveDiagnosticKind::MaterialInstanceValidationFailed,
                .assetId = metadata.id,
                .parentAssetId = parentMetadata->id,
                .path = ResolveAssetPhysicalPath(manager, metadata),
                .message = std::string{ RenderMaterialInstanceValidationDiagnosticCodeName(diagnostic.code) } + ": " + diagnostic.message,
            });
        }
        fallback.contentHash = runtimeContentHash;
        return fallback;
    }

    const RenderMaterialAssetData instanceMaterial = BuildEffectiveRenderMaterialInstanceAsset(*parentDocument.material, *instance);
    const std::vector<RenderMaterialGraphDiagnostic> instanceGraphDiagnostics = ValidateMaterialOutputRuntimeGraphDiagnostics(instanceMaterial);
    const bool instanceGraphHasError = std::any_of(instanceGraphDiagnostics.begin(), instanceGraphDiagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
    if (instanceGraphHasError) {
        ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
            RuntimeMaterialResolveStatus::ErrorMaterial,
            RuntimeMaterialResolveDiagnosticKind::MaterialGraphValidationFailed,
            RuntimeMaterialResolveDiagnosticSeverity::Error,
            metadata.id,
            ResolveAssetPhysicalPath(manager, metadata),
            "Material instance graph validation failed; using the error material.");
        fallback.contentHash = runtimeContentHash;
        fallback.failurePolicy = instanceMaterial.graph.artifactFailurePolicy;
        fallback.diagnostics.clear();
        AppendGraphValidationDiagnostics(fallback, instanceGraphDiagnostics, metadata.id, ResolveAssetPhysicalPath(manager, metadata));
        for (RuntimeMaterialResolveDiagnostic& diagnostic : fallback.diagnostics) {
            diagnostic.parentAssetId = parentMetadata->id;
        }
        return fallback;
    }

    ResolvedRuntimeMaterialAsset resolved{
        .material = ResolveLoadedMaterial(manager, metadata, instanceMaterial),
        .diagnostics = std::move(parentDocument.diagnostics),
        .contentHash = runtimeContentHash,
        .status = RuntimeMaterialResolveStatus::Resolved,
        .renderMode = RuntimeMaterialRenderMode::BuiltinPbr,
        .failurePolicy = instanceMaterial.graph.artifactFailurePolicy,
        .resolved = true,
    };
    if (HasGraphAuthoringData(instanceMaterial.graph)) {
        if (resolved.material.graphProgram.active) {
            resolved.renderMode = RuntimeMaterialRenderMode::GpuMaterialGraph;
        } else {
            resolved.renderMode = RuntimeMaterialRenderMode::CpuPbrFlatteningFallback;
            resolved.cpuFallbackReason = RuntimeMaterialCpuFallbackReason::GraphProgramUnavailable;
            resolved.diagnostics.push_back(RuntimeMaterialResolveDiagnostic{
                .severity = RuntimeMaterialResolveDiagnosticSeverity::Warning,
                .kind = RuntimeMaterialResolveDiagnosticKind::MaterialGraphValidationFailed,
                .assetId = metadata.id,
                .parentAssetId = parentMetadata->id,
                .path = ResolveAssetPhysicalPath(manager, metadata),
                .message = "Material instance graph has no GPU program; falling back to CPU PBR flattening.",
            });
            AppendGraphValidationDiagnostics(resolved, resolved.material.graphDiagnostics, metadata.id, ResolveAssetPhysicalPath(manager, metadata));
        }
    }
    for (RuntimeMaterialResolveDiagnostic& diagnostic : resolved.diagnostics) {
        if (!diagnostic.parentAssetId.IsValid()) {
            diagnostic.parentAssetId = parentMetadata->id;
        }
        if (!diagnostic.assetId.IsValid()) {
            diagnostic.assetId = metadata.id;
        }
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
    if (!IsSafeTextureReferencePath(textureVirtualPath)) {
        return 0U;
    }
    const std::filesystem::path candidate = textureVirtualPath.is_absolute()
        ? textureVirtualPath
        : (ownerMetadata.virtualPath.parent_path() / textureVirtualPath).lexically_normal();
    const kb::assets::AssetMetadata* textureMetadata = manager.Registry().FindByPath(candidate);
    if (textureMetadata == nullptr || !IsRuntimeTextureAsset(*textureMetadata)) {
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
