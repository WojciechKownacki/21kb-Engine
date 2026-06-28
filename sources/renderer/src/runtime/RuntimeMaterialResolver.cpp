#include "kb/render/runtime/RuntimeMaterialResolver.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
            .message = ParseDiagnosticMessage(diagnostic),
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

void ApplyGraphParameterValuesToPbrDesc(RenderMaterialDesc& desc, const std::vector<RenderMaterialGraphParameterValue>& values) {
    for (const RenderMaterialGraphParameterValue& value : values) {
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

[[nodiscard]] bool HasGraphAuthoringData(const RenderMaterialGraphDocument& graph) noexcept;

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
                ApplyTextureAssetIdByRole(desc, slot.role, value.assetId);
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

[[nodiscard]] std::string StableGraphParameterIdForRuntime(const RenderMaterialGraphNode& node) {
    if (!node.parameter.stableId.empty()) {
        return node.parameter.stableId;
    }
    switch (node.kind) {
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return "texture" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::TextureSample:
        return "textureSample" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return "scalar" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterVector:
        return "vector" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::ParameterColor:
        return "color" + std::to_string(node.id);
    case RenderMaterialGraphNodeKind::MaterialOutput:
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantVector:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::Add:
    case RenderMaterialGraphNodeKind::Multiply:
    case RenderMaterialGraphNodeKind::Clamp:
    case RenderMaterialGraphNodeKind::Lerp:
    case RenderMaterialGraphNodeKind::NormalUnpack:
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
        if (source != nullptr && source->kind == RenderMaterialGraphNodeKind::ParameterTexture) {
            return GraphTextureValueAssetId(materialAsset.graphParameterValues, StableGraphParameterIdForRuntime(*source));
        }
        return std::nullopt;
    }
    return GraphTextureValueAssetId(materialAsset.graphParameterValues, StableGraphParameterIdForRuntime(textureSample));
}

void ApplyMaterialOutputTextureLinksToPbrDesc(RenderMaterialDesc& desc, const RenderMaterialAssetData& materialAsset) {
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

    const RenderMaterialGraphLink* baseColor = FindGraphInputLink(materialAsset.graph, output->id, "baseColor");
    if (baseColor == nullptr) {
        desc.baseColor[0] = 0.0F;
        desc.baseColor[1] = 0.0F;
        desc.baseColor[2] = 0.0F;
        desc.baseColor[3] = 1.0F;
        desc.albedoTextureAssetId = 0U;
        return;
    }

    if (baseColor->fromPin == "color") {
        const RenderMaterialGraphNode* source = FindRenderMaterialGraphNode(materialAsset.graph, baseColor->fromNodeId);
        if (source != nullptr && source->kind == RenderMaterialGraphNodeKind::TextureSample) {
            if (const std::optional<std::uint64_t> textureAssetId = TextureAssetIdForTextureSampleNode(materialAsset, *source);
                textureAssetId.has_value()) {
                desc.baseColor[0] = 1.0F;
                desc.baseColor[1] = 1.0F;
                desc.baseColor[2] = 1.0F;
                desc.baseColor[3] = 1.0F;
                desc.albedoTextureAssetId = *textureAssetId;
            }
        }
    }
}

[[nodiscard]] bool IsImplicitDefaultGraphOutput(const RenderMaterialGraphNode& node) noexcept {
    return node.id == 1U &&
        node.kind == RenderMaterialGraphNodeKind::MaterialOutput &&
        node.positionX == 640 &&
        node.positionY == 240 &&
        node.parameter.stableId.empty() &&
        node.parameter.displayName.empty();
}

[[nodiscard]] bool HasGraphAuthoringData(const RenderMaterialGraphDocument& graph) noexcept {
    if (!graph.links.empty()) {
        return true;
    }
    return std::any_of(graph.nodes.begin(), graph.nodes.end(), [](const RenderMaterialGraphNode& node) {
        return !IsImplicitDefaultGraphOutput(node);
    });
}

void InheritMissingTextureAssetIds(RenderMaterialDesc& material, const RenderMaterialDesc& parent) noexcept {
    material.albedoTextureAssetId = material.albedoTextureAssetId != 0U ? material.albedoTextureAssetId : parent.albedoTextureAssetId;
    material.normalTextureAssetId = material.normalTextureAssetId != 0U ? material.normalTextureAssetId : parent.normalTextureAssetId;
    material.metallicRoughnessTextureAssetId = material.metallicRoughnessTextureAssetId != 0U ? material.metallicRoughnessTextureAssetId : parent.metallicRoughnessTextureAssetId;
    material.occlusionTextureAssetId = material.occlusionTextureAssetId != 0U ? material.occlusionTextureAssetId : parent.occlusionTextureAssetId;
    material.emissiveTextureAssetId = material.emissiveTextureAssetId != 0U ? material.emissiveTextureAssetId : parent.emissiveTextureAssetId;
    material.clearcoatTextureAssetId = material.clearcoatTextureAssetId != 0U ? material.clearcoatTextureAssetId : parent.clearcoatTextureAssetId;
    material.clearcoatRoughnessTextureAssetId = material.clearcoatRoughnessTextureAssetId != 0U ? material.clearcoatRoughnessTextureAssetId : parent.clearcoatRoughnessTextureAssetId;
    material.sheenColorTextureAssetId = material.sheenColorTextureAssetId != 0U ? material.sheenColorTextureAssetId : parent.sheenColorTextureAssetId;
    material.transmissionTextureAssetId = material.transmissionTextureAssetId != 0U ? material.transmissionTextureAssetId : parent.transmissionTextureAssetId;
    material.thicknessTextureAssetId = material.thicknessTextureAssetId != 0U ? material.thicknessTextureAssetId : parent.thicknessTextureAssetId;
    material.anisotropyTextureAssetId = material.anisotropyTextureAssetId != 0U ? material.anisotropyTextureAssetId : parent.anisotropyTextureAssetId;
    material.decalTextureAssetId = material.decalTextureAssetId != 0U ? material.decalTextureAssetId : parent.decalTextureAssetId;
    material.layerMaskTextureAssetId = material.layerMaskTextureAssetId != 0U ? material.layerMaskTextureAssetId : parent.layerMaskTextureAssetId;
}

void InheritMissingTexturePaths(RenderMaterialAssetData& material, const RenderMaterialAssetData& parent) {
    if (material.albedoTexturePath.empty()) material.albedoTexturePath = parent.albedoTexturePath;
    if (material.normalTexturePath.empty()) material.normalTexturePath = parent.normalTexturePath;
    if (material.metallicRoughnessTexturePath.empty()) material.metallicRoughnessTexturePath = parent.metallicRoughnessTexturePath;
    if (material.occlusionTexturePath.empty()) material.occlusionTexturePath = parent.occlusionTexturePath;
    if (material.emissiveTexturePath.empty()) material.emissiveTexturePath = parent.emissiveTexturePath;
    if (material.clearcoatTexturePath.empty()) material.clearcoatTexturePath = parent.clearcoatTexturePath;
    if (material.clearcoatRoughnessTexturePath.empty()) material.clearcoatRoughnessTexturePath = parent.clearcoatRoughnessTexturePath;
    if (material.sheenColorTexturePath.empty()) material.sheenColorTexturePath = parent.sheenColorTexturePath;
    if (material.transmissionTexturePath.empty()) material.transmissionTexturePath = parent.transmissionTexturePath;
    if (material.thicknessTexturePath.empty()) material.thicknessTexturePath = parent.thicknessTexturePath;
    if (material.anisotropyTexturePath.empty()) material.anisotropyTexturePath = parent.anisotropyTexturePath;
    if (material.decalTexturePath.empty()) material.decalTexturePath = parent.decalTexturePath;
    if (material.layerMaskTexturePath.empty()) material.layerMaskTexturePath = parent.layerMaskTexturePath;
}

void MergeGraphParameterValues(
    std::vector<RenderMaterialGraphParameterValue>& materialValues,
    const std::vector<RenderMaterialGraphParameterValue>& parentValues) {
    if (materialValues.empty()) {
        materialValues = parentValues;
        return;
    }

    std::vector<RenderMaterialGraphParameterValue> merged = parentValues;
    for (const RenderMaterialGraphParameterValue& overrideValue : materialValues) {
        const auto existing = std::find_if(merged.begin(), merged.end(), [&overrideValue](const RenderMaterialGraphParameterValue& value) {
            return value.stableId == overrideValue.stableId;
        });
        if (existing != merged.end()) {
            *existing = overrideValue;
        } else {
            merged.push_back(overrideValue);
        }
    }
    materialValues = std::move(merged);
}

[[nodiscard]] RenderMaterialAssetData BuildResolvedMaterialInstanceAsset(
    const RenderMaterialAssetData& parent,
    const RenderMaterialInstanceAssetData& instance) {
    if (!instance.hasOverrides) {
        return parent;
    }

    RenderMaterialAssetData material = instance.overrides;
    material.materialTypeAssetId = material.materialTypeAssetId != 0U ? material.materialTypeAssetId : parent.materialTypeAssetId;
    if (material.materialTypeAssetPath.empty()) material.materialTypeAssetPath = parent.materialTypeAssetPath;
    material.graphSourceAssetId = material.graphSourceAssetId != 0U ? material.graphSourceAssetId : parent.graphSourceAssetId;
    if (material.graphSourceAssetPath.empty()) material.graphSourceAssetPath = parent.graphSourceAssetPath;
    InheritMissingTextureAssetIds(material.desc, parent.desc);
    InheritMissingTexturePaths(material, parent);
    if (!HasGraphAuthoringData(material.graph) && HasGraphAuthoringData(parent.graph)) {
        material.graph = parent.graph;
    }
    MergeGraphParameterValues(material.graphParameterValues, parent.graphParameterValues);
    return material;
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

std::uint64_t RuntimeMaterialResolver::MaterialRuntimeContentHash(
    kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata) {
    std::uint64_t hash = metadata.contentHash;
    if (metadata.type == "RenderMaterialInstance") {
        const kb::assets::AssetHandle<RenderMaterialInstanceAssetData> instance = manager.Load<RenderMaterialInstanceAssetData>(metadata.id);
        if (!instance.IsLoaded() || !instance->parentMaterialAssetId.IsValid()) {
            return hash;
        }
        const kb::assets::AssetMetadata* parentMetadata = manager.Registry().Find(instance->parentMaterialAssetId);
        return parentMetadata == nullptr ? hash : HashCombine(hash, MaterialRuntimeContentHash(manager, *parentMetadata));
    }
    if (metadata.type != "RenderMaterial") {
        return hash;
    }

    for (const kb::assets::AssetId dependency : metadata.dependencies) {
        const kb::assets::AssetMetadata* dependencyMetadata = manager.Registry().Find(dependency);
        if (dependencyMetadata == nullptr || dependencyMetadata->type == "RenderTexture") {
            continue;
        }
        hash = HashCombine(hash, dependencyMetadata->id.value);
        hash = HashCombine(hash, dependencyMetadata->contentHash);
    }
    return hash;
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
    ApplyGraphParameterValuesToPbrDesc(resolved.desc, materialAsset.graphParameterValues);
    ApplyGraphTextureSlotValuesToPbrDesc(resolved.desc, materialAsset);
    ApplyMaterialOutputTextureLinksToPbrDesc(resolved.desc, materialAsset);
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
        const RenderMaterialAssetParseResult loaded = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(path, metadata.id);
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
            fallback.diagnostics.clear();
            AppendParseDiagnostics(fallback, loaded, metadata.id);
            AppendMaterialTypeReferenceDiagnostics(fallback, typeReferenceValidation, metadata.id, path);
            return fallback;
        }

        const std::vector<RenderMaterialGraphDiagnostic> graphDiagnostics = ValidateRenderMaterialAssetGraphDiagnostics(*loaded.asset);
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
            fallback.diagnostics.clear();
            AppendParseDiagnostics(fallback, loaded, metadata.id);
            AppendGraphValidationDiagnostics(fallback, graphDiagnostics, metadata.id, path);
            return fallback;
        }

        ResolvedRuntimeMaterialAsset resolved{
            .material = ResolveLoadedMaterial(manager, metadata, *loaded.asset),
            .contentHash = runtimeContentHash,
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
    if (parentMetadata == nullptr || parentMetadata->type != "RenderMaterial") {
        ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
            RuntimeMaterialResolveStatus::ErrorMaterial,
            RuntimeMaterialResolveDiagnosticKind::MissingParentMaterial,
            RuntimeMaterialResolveDiagnosticSeverity::Error,
            metadata.id,
            ResolveAssetPhysicalPath(manager, metadata),
            "Material instance parent is missing or is not a material; using the error material.");
        fallback.diagnostics.front().parentAssetId = instance->parentMaterialAssetId;
        fallback.contentHash = runtimeContentHash;
        return fallback;
    }
    const std::filesystem::path parentPath = ResolveAssetPhysicalPath(manager, *parentMetadata);
    const RenderMaterialAssetParseResult parentMaterial = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(parentPath, parentMetadata->id);
    if (!parentMaterial.asset.has_value()) {
        ResolvedRuntimeMaterialAsset fallback = FallbackMaterial(
            RuntimeMaterialResolveStatus::ErrorMaterial,
            RuntimeMaterialResolveDiagnosticKind::ParentMaterialLoadFailed,
            RuntimeMaterialResolveDiagnosticSeverity::Error,
            metadata.id,
            ResolveAssetPhysicalPath(manager, metadata),
            "Material instance parent could not be loaded; using the error material.");
        fallback.diagnostics.clear();
        AppendParseDiagnostics(fallback, parentMaterial, parentMetadata->id);
        for (RuntimeMaterialResolveDiagnostic& diagnostic : fallback.diagnostics) {
            diagnostic.assetId = metadata.id;
            diagnostic.parentAssetId = parentMetadata->id;
        }
        fallback.contentHash = runtimeContentHash;
        return fallback;
    }
    const RenderMaterialInstanceValidationResult validation = RenderMaterialInstanceAssetLoader::ValidateAgainstParent(*instance, *parentMaterial.asset);
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
    ResolvedRuntimeMaterialAsset parent = ResolveAsset(manager, *parentMetadata);
    if (!parent.resolved) {
        return parent;
    }

    const RenderMaterialAssetData instanceMaterial = BuildResolvedMaterialInstanceAsset(*parentMaterial.asset, *instance);
    const std::vector<RenderMaterialGraphDiagnostic> instanceGraphDiagnostics = ValidateRenderMaterialAssetGraphDiagnostics(instanceMaterial);
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
        fallback.diagnostics.clear();
        AppendGraphValidationDiagnostics(fallback, instanceGraphDiagnostics, metadata.id, ResolveAssetPhysicalPath(manager, metadata));
        for (RuntimeMaterialResolveDiagnostic& diagnostic : fallback.diagnostics) {
            diagnostic.parentAssetId = parentMetadata->id;
        }
        return fallback;
    }

    ResolvedRuntimeMaterialAsset resolved{
        .material = instance->hasOverrides ? ResolveLoadedMaterial(manager, metadata, instanceMaterial) : parent.material,
        .diagnostics = std::move(parent.diagnostics),
        .contentHash = runtimeContentHash,
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
