#include "RendererTestSupport.hpp"

#include "../src/resources/RenderMaterialAssetParser.hpp"
#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialGraphProgramBindingBuilder.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "kb/render/resources/RenderMaterialTextureSlots.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"
#include "../src/scene/pipeline/MeshPipelinePassPolicy.hpp"
#include "../src/scene/submit/SceneMeshMaterialBindingResolver.hpp"

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace kb::render::tests {
namespace {

[[nodiscard]] bool HasGraphDiagnostic(
    const std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    RenderMaterialGraphDiagnosticKind kind) {
    for (const RenderMaterialGraphDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.kind == kind) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] const RenderMaterialGraphDiagnostic* FindGraphDiagnostic(
    const std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    RenderMaterialGraphDiagnosticKind kind) {
    for (const RenderMaterialGraphDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.kind == kind) {
            return &diagnostic;
        }
    }
    return nullptr;
}

[[nodiscard]] const RenderMaterialAssetParseDiagnostic* FindParseDiagnostic(
    const std::vector<RenderMaterialAssetParseDiagnostic>& diagnostics,
    RenderMaterialAssetParseDiagnosticCode code) {
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            return &diagnostic;
        }
    }
    return nullptr;
}

[[nodiscard]] std::filesystem::path FindRepositoryFile(const std::filesystem::path& relativePath) {
    std::filesystem::path cursor = std::filesystem::current_path();
    for (std::size_t depth = 0U; depth < 6U; ++depth) {
        const std::filesystem::path candidate = cursor / relativePath;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!cursor.has_parent_path() || cursor == cursor.parent_path()) {
            break;
        }
        cursor = cursor.parent_path();
    }
    return {};
}

[[nodiscard]] std::string ReadRequiredTextFile(const std::filesystem::path& path) {
    std::ifstream input{ path };
    Require(input.good(), "Required repository text file could not be opened");
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

[[nodiscard]] RenderMaterialGraphLink MakeGraphLink(
    RenderMaterialGraphNodeKind fromKind,
    std::uint32_t fromNodeId,
    std::string fromPin,
    RenderMaterialGraphNodeKind toKind,
    std::uint32_t toNodeId,
    std::string toPin) {
    RenderMaterialGraphLink link{
        .fromNodeId = fromNodeId,
        .fromPinId = RenderMaterialGraphStablePinId(fromKind, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNodeId,
        .toPinId = RenderMaterialGraphStablePinId(toKind, toPin, false),
        .toPin = std::move(toPin),
    };
    link.id = MakeRenderMaterialGraphLinkId(link);
    return link;
}

[[nodiscard]] RenderMaterialGraphLink MakeGraphLink(
    const RenderMaterialGraphNode& fromNode,
    std::string fromPin,
    const RenderMaterialGraphNode& toNode,
    std::string toPin) {
    RenderMaterialGraphLink link{
        .fromNodeId = fromNode.id,
        .fromPinId = RenderMaterialGraphStablePinId(fromNode, fromPin, true),
        .fromPin = std::move(fromPin),
        .toNodeId = toNode.id,
        .toPinId = RenderMaterialGraphStablePinId(toNode, toPin, false),
        .toPin = std::move(toPin),
    };
    link.id = MakeRenderMaterialGraphLinkId(link);
    return link;
}

void RunBuiltInPbrMaterialTypeSchemaExistsTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();
    Require(schema.typeName == "builtin.pbr", "Built-in PBR schema has wrong type name");
    Require(schema.typeVersion == 1U, "Built-in PBR schema has wrong type version");
    Require(!schema.parameters.empty(), "Built-in PBR schema has no parameters");
    Require(!schema.textureSlots.empty(), "Built-in PBR schema has no texture slots");
    Require(!schema.alphaModes.empty(), "Built-in PBR schema has no alpha modes");
}

void RunBuiltInPbrSchemaCoversAllMaterialFieldsTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();

    // Core MVP parameters must be present
    Require(FindMaterialParameterSchema(schema, "baseColor") != nullptr, "Schema missing baseColor");
    Require(FindMaterialParameterSchema(schema, "metallicFactor") != nullptr, "Schema missing metallicFactor");
    Require(FindMaterialParameterSchema(schema, "roughnessFactor") != nullptr, "Schema missing roughnessFactor");
    Require(FindMaterialParameterSchema(schema, "normalScale") != nullptr, "Schema missing normalScale");
    Require(FindMaterialParameterSchema(schema, "occlusionStrength") != nullptr, "Schema missing occlusionStrength");
    Require(FindMaterialParameterSchema(schema, "emissiveColor") != nullptr, "Schema missing emissiveColor");
    Require(FindMaterialParameterSchema(schema, "emissiveStrength") != nullptr, "Schema missing emissiveStrength");
    Require(FindMaterialParameterSchema(schema, "alphaMode") != nullptr, "Schema missing alphaMode");
    Require(FindMaterialParameterSchema(schema, "alphaCutoff") != nullptr, "Schema missing alphaCutoff");
    Require(FindMaterialParameterSchema(schema, "doubleSided") != nullptr, "Schema missing doubleSided");

    // Advanced parameters must be present
    Require(FindMaterialParameterSchema(schema, "clearcoatFactor") != nullptr, "Schema missing clearcoatFactor");
    Require(FindMaterialParameterSchema(schema, "transmissionFactor") != nullptr, "Schema missing transmissionFactor");
    Require(FindMaterialParameterSchema(schema, "sheenColor") != nullptr, "Schema missing sheenColor");
    Require(FindMaterialParameterSchema(schema, "anisotropyStrength") != nullptr, "Schema missing anisotropyStrength");

    // Texture slots must be present
    Require(FindMaterialTextureSlotSchema(schema, "albedoTextureAssetId") != nullptr, "Schema missing albedo texture slot");
    Require(FindMaterialTextureSlotSchema(schema, "normalTextureAssetId") != nullptr, "Schema missing normal texture slot");
    Require(FindMaterialTextureSlotSchema(schema, "metallicRoughnessTextureAssetId") != nullptr, "Schema missing metallic-roughness texture slot");
    Require(FindMaterialTextureSlotSchema(schema, "occlusionTextureAssetId") != nullptr, "Schema missing occlusion texture slot");
    Require(FindMaterialTextureSlotSchema(schema, "emissiveTextureAssetId") != nullptr, "Schema missing emissive texture slot");
}

void RunBuiltInPbrSchemaHasCorrectRangesTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();

    const RenderMaterialParameterSchema* baseColor = FindMaterialParameterSchema(schema, "baseColor");
    Require(baseColor != nullptr && baseColor->range.has_value(), "Schema baseColor missing range");
    Require(baseColor->range->min == 0.0F && baseColor->range->max == 1.0F, "Schema baseColor has wrong range");

    const RenderMaterialParameterSchema* metallic = FindMaterialParameterSchema(schema, "metallicFactor");
    Require(metallic != nullptr && metallic->range.has_value(), "Schema metallicFactor missing range");
    Require(metallic->range->min == 0.0F && metallic->range->max == 1.0F, "Schema metallicFactor has wrong range");

    const RenderMaterialParameterSchema* roughness = FindMaterialParameterSchema(schema, "roughnessFactor");
    Require(roughness != nullptr && roughness->range.has_value(), "Schema roughnessFactor missing range");
    Require(roughness->range->min == 0.0F && roughness->range->max == 1.0F, "Schema roughnessFactor has wrong range");

    const RenderMaterialParameterSchema* normalScale = FindMaterialParameterSchema(schema, "normalScale");
    Require(normalScale != nullptr && normalScale->range.has_value(), "Schema normalScale missing range");
    Require(normalScale->range->min == 0.0F && normalScale->range->max == 8.0F, "Schema normalScale has wrong range");

    const RenderMaterialParameterSchema* emissiveStrength = FindMaterialParameterSchema(schema, "emissiveStrength");
    Require(emissiveStrength != nullptr && emissiveStrength->range.has_value(), "Schema emissiveStrength missing range");
    Require(emissiveStrength->range->min == 0.0F && emissiveStrength->range->max == 64.0F, "Schema emissiveStrength has wrong range");
}

void RunBuiltInPbrSchemaTextureSlotsHaveColorSpaceTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();

    const RenderMaterialTextureSlotSchema* albedo = FindMaterialTextureSlotSchema(schema, "albedoTextureAssetId");
    Require(albedo != nullptr, "Schema missing albedo texture slot");
    Require(albedo->expectedColorSpace == RenderMaterialTextureColorSpace::Srgb, "Albedo texture should be sRGB");
    Require(albedo->runtimeSupport == RenderMaterialFeatureSupport::Supported, "Albedo should be supported");

    const RenderMaterialTextureSlotSchema* normal = FindMaterialTextureSlotSchema(schema, "normalTextureAssetId");
    Require(normal != nullptr, "Schema missing normal texture slot");
    Require(normal->expectedColorSpace == RenderMaterialTextureColorSpace::Linear, "Normal texture should be linear");

    const RenderMaterialTextureSlotSchema* mr = FindMaterialTextureSlotSchema(schema, "metallicRoughnessTextureAssetId");
    Require(mr != nullptr, "Schema missing metallic-roughness texture slot");
    Require(mr->expectedColorSpace == RenderMaterialTextureColorSpace::Linear, "Metallic-roughness texture should be linear");

    const RenderMaterialTextureSlotSchema* occlusion = FindMaterialTextureSlotSchema(schema, "occlusionTextureAssetId");
    Require(occlusion != nullptr, "Schema missing occlusion texture slot");
    Require(occlusion->expectedColorSpace == RenderMaterialTextureColorSpace::Linear, "Occlusion texture should be linear");

    const RenderMaterialTextureSlotSchema* emissive = FindMaterialTextureSlotSchema(schema, "emissiveTextureAssetId");
    Require(emissive != nullptr, "Schema missing emissive texture slot");
    Require(emissive->expectedColorSpace == RenderMaterialTextureColorSpace::Srgb, "Emissive texture should be sRGB");
}

void RunKbmat1009MaterialTextureSlotColorSpaceRuntimeGateTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();
    Require(schema.textureSlots.size() == kRenderMaterialTextureSlotPolicies.size(), "KBMAT-1009: runtime texture policy table must cover every schema texture slot");

    std::size_t supportedSlotCount = 0U;
    std::size_t supportedSrgbSlotCount = 0U;
    std::size_t supportedLinearSlotCount = 0U;
    for (const RenderMaterialTextureSlotPolicy policy : kRenderMaterialTextureSlotPolicies) {
        const RenderMaterialTextureSlotSchema* schemaSlot = FindMaterialTextureSlotSchema(schema, policy.assetIdFieldName);
        Require(schemaSlot != nullptr, "KBMAT-1009: runtime texture policy references a slot missing from schema");
        Require(schemaSlot->expectedColorSpace == policy.expectedColorSpace, "KBMAT-1009: runtime texture policy color space diverged from schema");
        Require(schemaSlot->runtimeSupport == policy.runtimeSupport, "KBMAT-1009: runtime texture policy support flag diverged from schema");

        if (policy.runtimeSupport == RenderMaterialFeatureSupport::Supported) {
            ++supportedSlotCount;
            const RenderTextureColorSpace bindingColorSpace = RenderTextureBindingColorSpace(policy.expectedColorSpace);
            if (bindingColorSpace == RenderTextureColorSpace::Srgb) {
                ++supportedSrgbSlotCount;
            } else {
                ++supportedLinearSlotCount;
            }
            Require(policy.expectedColorSpace != RenderMaterialTextureColorSpace::Unknown, "KBMAT-1009: runtime-supported texture slots need explicit color-space policy");
        }
    }
    Require(supportedSlotCount == 5U, "KBMAT-1009: runtime should bind the five MVP PBR texture slots");
    Require(supportedSrgbSlotCount == 2U, "KBMAT-1009: only Base Color and Emissive runtime slots should bind as sRGB");
    Require(supportedLinearSlotCount == 3U, "KBMAT-1009: Normal, Metallic-Roughness and Occlusion runtime slots should bind as linear");

    RenderMaterialResource material{};
    material.albedoTextureAssetId = 11U;
    material.normalTextureAssetId = 12U;
    material.metallicRoughnessTextureAssetId = 13U;
    material.occlusionTextureAssetId = 14U;
    material.emissiveTextureAssetId = 15U;
    material.sheenColorTextureAssetId = 18U;
    material.albedoTexture = RenderTextureHandle{ 101U };
    material.normalTexture = RenderTextureHandle{ 102U };
    material.metallicRoughnessTexture = RenderTextureHandle{ 103U };
    material.occlusionTexture = RenderTextureHandle{ 104U };
    material.emissiveTexture = RenderTextureHandle{ 105U };
    material.sheenColorTexture = RenderTextureHandle{ 108U };

    const std::array<RenderMaterialTextureSlotBinding, kRenderMaterialTextureSlotPolicies.size()> slots = RenderMaterialTextureSlots(material);
    Require(slots.size() == kRenderMaterialTextureSlotPolicies.size(), "KBMAT-1009: runtime material texture slot list has unexpected size");
    Require(slots[0].policy.kind == RenderMaterialTextureSlotKind::Albedo && slots[0].assetId == 11U && slots[0].directHandle.value == 101U &&
            RenderTextureBindingColorSpace(slots[0].policy.expectedColorSpace) == RenderTextureColorSpace::Srgb,
        "KBMAT-1009: albedo runtime slot did not preserve sRGB policy and binding data");
    Require(slots[1].policy.kind == RenderMaterialTextureSlotKind::Normal && slots[1].assetId == 12U && slots[1].directHandle.value == 102U &&
            RenderTextureBindingColorSpace(slots[1].policy.expectedColorSpace) == RenderTextureColorSpace::Linear,
        "KBMAT-1009: normal runtime slot did not preserve linear policy and binding data");
    Require(slots[2].policy.kind == RenderMaterialTextureSlotKind::MetallicRoughness && slots[2].assetId == 13U &&
            RenderTextureBindingColorSpace(slots[2].policy.expectedColorSpace) == RenderTextureColorSpace::Linear,
        "KBMAT-1009: metallic-roughness runtime slot did not preserve linear policy");
    Require(slots[3].policy.kind == RenderMaterialTextureSlotKind::Occlusion && slots[3].assetId == 14U &&
            RenderTextureBindingColorSpace(slots[3].policy.expectedColorSpace) == RenderTextureColorSpace::Linear,
        "KBMAT-1009: occlusion runtime slot did not preserve linear policy");
    Require(slots[4].policy.kind == RenderMaterialTextureSlotKind::Emissive && slots[4].assetId == 15U &&
            RenderTextureBindingColorSpace(slots[4].policy.expectedColorSpace) == RenderTextureColorSpace::Srgb,
        "KBMAT-1009: emissive runtime slot did not preserve sRGB policy");
    Require(RenderMaterialTextureSlot(material, RenderMaterialTextureSlotKind::SheenColor).assetId == 18U &&
            RenderMaterialTextureSlot(material, RenderMaterialTextureSlotKind::SheenColor).policy.expectedColorSpace == RenderMaterialTextureColorSpace::Srgb,
        "KBMAT-1009: parsed advanced texture slots must keep their schema color-space policy even when ignored by MVP runtime");
}

void RunBuiltInPbrMaterialTypeDocumentExistsTest() {
    const RenderMaterialTypeDocument& document = GetBuiltInPbrMaterialTypeDocument();
    Require(document.documentVersion == kRenderMaterialTypeDocumentVersion, "Built-in PBR material type document has wrong document version");
    Require(document.stableTypeId == "builtin.pbr", "Built-in PBR material type document has wrong stable id");
    Require(document.version == 1U, "Built-in PBR material type document has wrong version");
    Require(!document.displayName.empty(), "Built-in PBR material type document has no editor display name");
    Require(document.domain == RenderMaterialDomain::Surface, "KBMAT-GRAPH-0002: Built-in PBR material type must declare Surface domain");
    Require(document.shaderModel == RenderMaterialShaderModel::MetallicRoughnessPbr, "KBMAT-GRAPH-0002: Built-in PBR material type must declare metallic-roughness shader model");
    Require(document.defaultBlendMode == RenderMaterialBlendMode::Opaque, "KBMAT-GRAPH-0002: Built-in PBR material type must declare default opaque blend mode");
    Require(document.defaultCullMode == RenderMaterialCullMode::BackFace, "KBMAT-GRAPH-0002: Built-in PBR material type must declare default cull mode");
    Require(document.renderPasses.size() == 6U, "KBMAT-GRAPH-0002: Built-in PBR material type should declare render pass support");
    Require(document.permutationKeys.size() == 3U, "KBMAT-GRAPH-0002: Built-in PBR material type should declare permutation keys");
    Require(document.requiredResources.size() >= 4U, "KBMAT-GRAPH-0002: Built-in PBR material type should declare shader resources");
    Require(document.schema.typeName == document.stableTypeId, "Built-in PBR material type document schema id mismatch");
    Require(document.schema.typeVersion == document.version, "Built-in PBR material type document schema version mismatch");

    const auto hasPass = [&document](std::string_view pass, RenderMaterialFeatureSupport support) {
        for (const RenderMaterialTypeRenderPass& renderPass : document.renderPasses) {
            if (renderPass.name == pass && renderPass.support == support && !renderPass.vertexShader.empty() && !renderPass.fragmentShader.empty()) {
                return true;
            }
        }
        return false;
    };
    Require(hasPass("BaseOpaque", RenderMaterialFeatureSupport::Supported), "KBMAT-GRAPH-0002: Built-in PBR material type missing BaseOpaque pass");
    Require(hasPass("GBuffer", RenderMaterialFeatureSupport::Supported), "Deferred: Built-in PBR material type missing GBuffer pass");
    Require(hasPass("ShadowDepth", RenderMaterialFeatureSupport::Supported), "KBMAT-GRAPH-0002: Built-in PBR material type missing ShadowDepth pass");
    Require(hasPass("SelectionId", RenderMaterialFeatureSupport::Supported), "KBMAT-GRAPH-0002: Built-in PBR material type missing SelectionId pass");
    Require(hasPass("BaseTransparent", RenderMaterialFeatureSupport::Supported), "KBMAT-MAT80: Built-in PBR material type must declare the transparent pass as supported");

    const auto hasPermutation = [&document](std::string_view key, std::string_view defaultValue) {
        for (const RenderMaterialTypePermutationKey& permutation : document.permutationKeys) {
            if (permutation.name == key && permutation.defaultValue == defaultValue && !permutation.allowedValues.empty()) {
                return true;
            }
        }
        return false;
    };
    Require(hasPermutation("alphaMode", "OPAQUE"), "KBMAT-GRAPH-0002: Built-in PBR material type missing alphaMode permutation key");
    Require(hasPermutation("doubleSided", "false"), "KBMAT-GRAPH-0002: Built-in PBR material type missing doubleSided permutation key");

    const auto hasResource = [&document](std::string_view name, std::string_view kind) {
        for (const RenderMaterialTypeRequiredResource& resource : document.requiredResources) {
            if (resource.name == name && resource.kind == kind) {
                return true;
            }
        }
        return false;
    };
    Require(hasResource("vs_mesh_instanced", "vertexShader"), "KBMAT-GRAPH-0002: Built-in PBR material type missing base vertex shader resource");
    Require(hasResource("fs_mesh_instanced", "fragmentShader"), "KBMAT-GRAPH-0002: Built-in PBR material type missing base fragment shader resource");
    Require(hasResource("fs_mesh_gbuffer_instanced", "fragmentShader"), "Deferred: Built-in PBR material type missing GBuffer fragment shader resource");
    Require(hasResource("fs_mesh_shadow_instanced", "fragmentShader"), "KBMAT-GRAPH-0002: Built-in PBR material type missing shadow fragment shader resource");
    Require(ValidateRenderMaterialTypeDocument(document).Succeeded(), "KBMAT-GRAPH-0004: Built-in PBR Material Type document should pass version validation");
}

void RunMaterialVersioningContractsTest() {
    {
        std::istringstream input{
            "version 999\n"
            "materialType builtin.pbr\n"
            "materialTypeVersion 1\n"
            "baseColor 1 1 1 1\n"
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(!result.asset.has_value(), "KBMAT-GRAPH-0004: Future .kbmat version should fail parsing");
        Require(!result.diagnostics.empty() && result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedDocumentVersion,
            "KBMAT-GRAPH-0004: Future .kbmat version should report unsupported document version");
    }
    {
        std::istringstream input{
            "version 999\n"
            "parentMaterialAssetId 77\n"
        };
        const RenderMaterialInstanceAssetParseResult result = RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(input);
        Require(!result.asset.has_value(), "KBMAT-GRAPH-0004: Future .kbmatinst version should fail parsing");
        Require(!result.diagnostics.empty() && result.diagnostics[0].code == RenderMaterialInstanceAssetParseDiagnosticCode::UnsupportedDocumentVersion,
            "KBMAT-GRAPH-0004: Future .kbmatinst version should report unsupported document version");
    }
    {
        std::istringstream input{
            "version 1\n"
            "materialType builtin.pbr\n"
            "materialTypeVersion 1\n"
            "baseColor 1 1 1 1\n"
            "graphVersion 999\n"
            "graphNode 1 MaterialOutput 640 240\n"
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(!result.asset.has_value(), "KBMAT-GRAPH-0004: Future material graph version should fail parsing");
        Require(!result.diagnostics.empty() && result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedGraphVersion,
            "KBMAT-GRAPH-0004: Future material graph version should report unsupported graph version");
    }
    {
        std::istringstream input{
            "version 999\n"
            "stableTypeId graph.surface\n"
            "typeVersion 1\n"
        };
        const RenderMaterialTypeDocumentParseResult result = ParseRenderMaterialTypeDocument(input);
        Require(!result.document.has_value(), "KBMAT-GRAPH-0004: Future Material Type asset version should fail parsing");
        Require(!result.diagnostics.empty() && result.diagnostics[0].code == RenderMaterialTypeDocumentDiagnosticCode::UnsupportedDocumentVersion,
            "KBMAT-GRAPH-0004: Future Material Type asset version should report unsupported document version");
        Require(RenderMaterialTypeDocumentDiagnosticCodeName(result.diagnostics[0].code) == std::string_view{ "unsupported_document_version" },
            "KBMAT-GRAPH-0004: Material Type document diagnostics should expose stable code names");
    }
}

void RunMaterialTypeDocumentRoundTripTest() {
    const RenderMaterialTypeDocument& source = GetBuiltInPbrMaterialTypeDocument();
    std::ostringstream output;
    WriteRenderMaterialTypeDocument(output, source);
    Require(output.str().find("# KB material type\nversion 1\nstableTypeId builtin.pbr\ntypeVersion 1\n") == 0U,
        "KBMAT-GRAPH-0004: Material Type writer should emit canonical versioned header");
    Require(output.str().find("renderPass BaseOpaque Supported vs_mesh_instanced fs_mesh_instanced\n") != std::string::npos,
        "KBMAT-GRAPH-0004: Material Type writer should emit render pass contract");
    Require(output.str().find("textureSlot Base%20Color albedoTextureAssetId albedoTexture Srgb Supported Base%20color%20/%20albedo%20texture. White%20(1,1,1,1) 0 _ true\n") != std::string::npos,
        "KBMAT-GRAPH-0004: Material Type writer should preserve encoded texture slot display names");
    Require(output.str().find("migration RenameParameter 0 1 baseColorFactor baseColor _ glTF%20naming%20was%20normalized%20to%20the%20engine%20PBR%20baseColor%20parameter.\n") != std::string::npos,
        "KBMAT-GRAPH-0004: Material Type writer should emit migration rows");

    std::istringstream input{ output.str() };
    const RenderMaterialTypeDocumentParseResult result = ParseRenderMaterialTypeDocument(input);
    Require(result.document.has_value(), "KBMAT-GRAPH-0004: Material Type parser rejected writer output");
    Require(result.diagnostics.empty(), "KBMAT-GRAPH-0004: Material Type parser produced diagnostics for writer output");
    Require(result.document->documentVersion == kRenderMaterialTypeDocumentVersion, "KBMAT-GRAPH-0004: Material Type round-trip lost document version");
    Require(result.document->stableTypeId == source.stableTypeId && result.document->version == source.version, "KBMAT-GRAPH-0004: Material Type round-trip lost identity");
    Require(result.document->domain == source.domain && result.document->shaderModel == source.shaderModel, "KBMAT-GRAPH-0004: Material Type round-trip lost render model");
    Require(result.document->renderPasses.size() == source.renderPasses.size(), "KBMAT-GRAPH-0004: Material Type round-trip lost render passes");
    Require(result.document->permutationKeys.size() == source.permutationKeys.size(), "KBMAT-GRAPH-0004: Material Type round-trip lost permutation keys");
    Require(result.document->requiredResources.size() == source.requiredResources.size(), "KBMAT-GRAPH-0004: Material Type round-trip lost required resources");
    Require(result.document->schema.parameters.size() == source.schema.parameters.size(), "KBMAT-GRAPH-0004: Material Type round-trip lost parameter schema");
    Require(result.document->schema.textureSlots.size() == source.schema.textureSlots.size(), "KBMAT-GRAPH-0004: Material Type round-trip lost texture slot schema");
    Require(result.document->schema.migrations.size() == source.schema.migrations.size(), "KBMAT-GRAPH-0004: Material Type round-trip lost migration table");
    Require(result.document->schema.textureSlots[0].name == "Base Color", "KBMAT-GRAPH-0004: Material Type parser did not decode texture slot display name");
    Require(result.document->schema.textureSlots[0].description == "Base color / albedo texture.", "KBMAT-GRAPH-0004: Material Type parser did not decode texture slot description");
    Require(result.document->schema.textureSlots[0].fallbackDescription == "White (1,1,1,1)", "KBMAT-GRAPH-0004: Material Type parser did not decode texture slot fallback");
    Require(result.document->schema.textureSlots[0].role.empty() && result.document->schema.textureSlots[0].overrideSupported, "KBMAT-GRAPH-0105: Material Type round-trip lost texture slot role/override metadata");
    Require(result.document->schema.parameters[0].defaultValueHint == "1 1 1 1", "KBMAT-GRAPH-0004: Material Type parser did not decode parameter default value");
    Require(result.document->schema.parameters[0].description == "Base color and opacity (RGBA).", "KBMAT-GRAPH-0004: Material Type parser did not decode parameter description");
    Require(result.document->schema.parameters[0].overrideSupported, "KBMAT-GRAPH-0105: Material Type round-trip lost parameter override metadata");
    const RenderMaterialParameterSchema* alphaMode = FindMaterialParameterSchema(result.document->schema, "alphaMode");
    Require(alphaMode != nullptr && !alphaMode->range.has_value(), "KBMAT-GRAPH-0004: Material Type round-trip should preserve parameters without numeric range");
    const RenderMaterialTypeMigrationOperation* renameBaseColor = FindMaterialTypeMigration(
        result.document->schema, RenderMaterialTypeMigrationOperationKind::RenameParameter, "baseColorFactor");
    Require(renameBaseColor != nullptr && renameBaseColor->targetField == "baseColor" && renameBaseColor->fromVersion == 0U && renameBaseColor->toVersion == 1U,
        "KBMAT-GRAPH-0004: Material Type round-trip lost rename migration");
    const RenderMaterialTypeMigrationOperation* emissiveDefault = FindMaterialTypeMigration(
        result.document->schema, RenderMaterialTypeMigrationOperationKind::SetDefault, "emissiveStrength");
    Require(emissiveDefault != nullptr && emissiveDefault->defaultValue == "1",
        "KBMAT-GRAPH-0004: Material Type round-trip lost default-value migration");
}

void RunKbmat0602To0605PbrTextureSchemaTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();

    const RenderMaterialTextureSlotSchema* normal = FindMaterialTextureSlotSchema(schema, "normalTextureAssetId");
    Require(normal != nullptr, "KBMAT-0602: Normal texture slot is missing");
    Require(normal->expectedColorSpace == RenderMaterialTextureColorSpace::Linear, "KBMAT-0602: Normal texture must be linear");
    Require(normal->fallbackDescription.find("Flat normal") != std::string::npos, "KBMAT-0602: Normal texture must document flat-normal fallback");

    const RenderMaterialTextureSlotSchema* metallicRoughness = FindMaterialTextureSlotSchema(schema, "metallicRoughnessTextureAssetId");
    Require(metallicRoughness != nullptr, "KBMAT-0603: Metallic-Roughness texture slot is missing");
    Require(metallicRoughness->expectedColorSpace == RenderMaterialTextureColorSpace::Linear, "KBMAT-0603: Metallic-Roughness texture must be linear");
    Require(metallicRoughness->fallbackDescription.find("metallic=1, roughness=1") != std::string::npos, "KBMAT-0603: Metallic-Roughness texture must document white fallback");

    const RenderMaterialTextureSlotSchema* occlusion = FindMaterialTextureSlotSchema(schema, "occlusionTextureAssetId");
    Require(occlusion != nullptr, "KBMAT-0604: Occlusion texture slot is missing");
    Require(occlusion->expectedColorSpace == RenderMaterialTextureColorSpace::Linear, "KBMAT-0604: Occlusion texture must be linear");
    Require(occlusion->fallbackDescription.find("occlusion=1") != std::string::npos, "KBMAT-0604: Occlusion texture must document white fallback");

    const RenderMaterialTextureSlotSchema* emissive = FindMaterialTextureSlotSchema(schema, "emissiveTextureAssetId");
    Require(emissive != nullptr, "KBMAT-0605: Emissive texture slot is missing");
    Require(emissive->expectedColorSpace == RenderMaterialTextureColorSpace::Srgb, "KBMAT-0605: Emissive texture must have explicit sRGB policy");
    Require(emissive->fallbackDescription.find("emissiveColor * emissiveStrength") != std::string::npos, "KBMAT-0605: Emissive fallback must preserve color and strength");

    const RenderMaterialParameterSchema* emissiveStrength = FindMaterialParameterSchema(schema, "emissiveStrength");
    Require(emissiveStrength != nullptr && emissiveStrength->runtimeSupport == RenderMaterialFeatureSupport::Supported, "KBMAT-0605: emissiveStrength must be runtime-supported");
}

void RunKbmat0602To0605MaterialBindingRuntimeTest() {
    RenderMaterialDesc desc{};
    desc.albedoTextureAssetId = 101U;
    desc.normalTextureAssetId = 102U;
    desc.metallicRoughnessTextureAssetId = 103U;
    desc.occlusionTextureAssetId = 104U;
    desc.emissiveTextureAssetId = 105U;
    desc.metallicFactor = 0.7F;
    desc.roughnessFactor = 0.3F;
    desc.normalScale = 2.0F;
    desc.occlusionStrength = 0.45F;
    desc.emissiveColor[0] = 0.2F;
    desc.emissiveColor[1] = 0.4F;
    desc.emissiveColor[2] = 0.6F;
    desc.emissiveStrength = 2.0F;
    desc.uvTiling[0] = 2.0F;
    desc.uvTiling[1] = 3.0F;
    desc.uvOffset[0] = 0.25F;
    desc.uvOffset[1] = 0.5F;

    RenderResourceRegistry registry;
    const RenderMaterialHandle materialHandle = registry.RegisterMaterial(desc);
    Require(materialHandle.IsValid(), "KBMAT-0602..0605: Material registration failed before binding");
    const RenderMaterialResource* material = registry.FindMaterial(materialHandle);
    Require(material != nullptr, "KBMAT-0602..0605: Registered material did not resolve before binding");

    SceneRenderResourceMap resourceMap;
    resourceMap.BindTexture(desc.albedoTextureAssetId, RenderTextureColorSpace::Srgb, RenderTextureHandle{ 0x0000'0001'0000'0011ULL });
    resourceMap.BindTexture(desc.normalTextureAssetId, RenderTextureColorSpace::Linear, RenderTextureHandle{ 0x0000'0001'0000'0012ULL });
    resourceMap.BindTexture(desc.metallicRoughnessTextureAssetId, RenderTextureColorSpace::Linear, RenderTextureHandle{ 0x0000'0001'0000'0013ULL });
    resourceMap.BindTexture(desc.occlusionTextureAssetId, RenderTextureColorSpace::Linear, RenderTextureHandle{ 0x0000'0001'0000'0014ULL });
    resourceMap.BindTexture(desc.emissiveTextureAssetId, RenderTextureColorSpace::Srgb, RenderTextureHandle{ 0x0000'0001'0000'0015ULL });

    Require(resourceMap.ResolveTexture(desc.albedoTextureAssetId, RenderTextureColorSpace::Linear) !=
                resourceMap.ResolveTexture(desc.albedoTextureAssetId, RenderTextureColorSpace::Srgb),
            "KBMAT-0601: Base Color texture binding must be color-space specific");
    Require(resourceMap.ResolveTexture(desc.normalTextureAssetId, RenderTextureColorSpace::Srgb) !=
                resourceMap.ResolveTexture(desc.normalTextureAssetId, RenderTextureColorSpace::Linear),
            "KBMAT-0602: Normal texture binding must be resolved as linear");
    Require(resourceMap.ResolveTexture(desc.emissiveTextureAssetId, RenderTextureColorSpace::Linear) !=
                resourceMap.ResolveTexture(desc.emissiveTextureAssetId, RenderTextureColorSpace::Srgb),
            "KBMAT-0605: Emissive texture binding must be resolved through the explicit sRGB policy");

    const bgfx::TextureHandle whiteFallback{ 11U };
    const bgfx::TextureHandle normalFallback{ 12U };
    const SceneMeshMaterialBinding binding = SceneMeshMaterialBindingResolver::Resolve(
        material,
        registry,
        resourceMap,
        SceneMeshMaterialBindingFallbacks{
            .whiteTexture = whiteFallback,
            .normalTexture = normalFallback,
        });

    Require(binding.albedoTexture.idx == whiteFallback.idx, "KBMAT-0601: Missing registered Base Color GPU texture must use white fallback");
    Require(binding.normalTexture.idx == normalFallback.idx, "KBMAT-0602: Missing registered normal GPU texture must use flat-normal fallback");
    Require(binding.metallicRoughnessTexture.idx == whiteFallback.idx, "KBMAT-0603: Missing registered metallic-roughness GPU texture must use white fallback");
    Require(binding.occlusionTexture.idx == whiteFallback.idx, "KBMAT-0604: Missing registered occlusion GPU texture must use white fallback");
    Require(binding.emissiveTexture.idx == whiteFallback.idx, "KBMAT-0605: Missing emissive texture must use white passthrough fallback");
    Require(NearlyEqual(binding.params[0], desc.metallicFactor), "KBMAT-0603: Metallic factor was not preserved in runtime material params");
    Require(NearlyEqual(binding.params[1], desc.roughnessFactor), "KBMAT-0603: Roughness factor was not preserved in runtime material params");
    Require(NearlyEqual(binding.params[2], 0.0F), "KBMAT-0602: normalScale must be disabled when the normal texture does not resolve to a live GPU texture");
    Require(NearlyEqual(binding.flags[1], desc.occlusionStrength), "KBMAT-0604: Occlusion strength was not preserved in runtime material flags");
    Require(NearlyEqual(binding.uvTransform[0], desc.uvTiling[0]) && NearlyEqual(binding.uvTransform[1], desc.uvTiling[1]), "KBMAT-0701: Material UV tiling was not preserved in runtime material binding");
    Require(NearlyEqual(binding.uvTransform[2], desc.uvOffset[0]) && NearlyEqual(binding.uvTransform[3], desc.uvOffset[1]), "KBMAT-0701: Material UV offset was not preserved in runtime material binding");
    Require(binding.emissive[0] == desc.emissiveColor[0], "KBMAT-0605: Emissive red factor was not preserved");
    Require(binding.emissive[1] == desc.emissiveColor[1], "KBMAT-0605: Emissive green factor was not preserved");
    Require(binding.emissive[2] == desc.emissiveColor[2], "KBMAT-0605: Emissive blue factor was not preserved");
    Require(binding.emissive[3] == desc.emissiveStrength, "KBMAT-0605: Emissive strength was not preserved");
}

void RunKbmat0606OpaqueAlphaRuntimeTest() {
    RenderMaterialResource material{};
    material.alphaMode = RenderMaterialAlphaMode::Opaque;
    material.alphaCutoff = 0.75F;
    material.baseColor[3] = 0.2F;

    const SceneMeshMaterialBinding binding = SceneMeshMaterialBindingResolver::Resolve(
        &material,
        RenderResourceRegistry{},
        SceneRenderResourceMap{},
        SceneMeshMaterialBindingFallbacks{});
    Require(binding.flags[0] == 0.0F, "KBMAT-0606: Opaque alpha mode must be encoded as the non-discarding shader mode");
    Require(NearlyEqual(binding.params[3], material.alphaCutoff), "KBMAT-0606: Opaque binding must still preserve authored alpha cutoff for stable material state");

    const SceneRenderMeshInstance instance{ .castsShadow = true };
    Require(MeshPipelinePassPolicy::Accepts(MeshPassType::Depth, instance, &material, {}), "KBMAT-0606: Opaque material must remain accepted by the depth pass");
    Require(MeshPipelinePassPolicy::Accepts(MeshPassType::BaseOpaque, instance, &material, {}), "KBMAT-0606: Opaque material must remain accepted by the base opaque pass");
    Require(MeshPipelinePassPolicy::Accepts(MeshPassType::ShadowDepth, instance, &material, {}), "KBMAT-0606: Opaque material must remain accepted by the shadow pass");
}

void RunKbmat0607AlphaMaskCutoffRuntimeTest() {
    RenderMaterialResource material{};
    material.alphaMode = RenderMaterialAlphaMode::Mask;
    material.alphaCutoff = 0.37F;

    const SceneMeshMaterialBinding binding = SceneMeshMaterialBindingResolver::Resolve(
        &material,
        RenderResourceRegistry{},
        SceneRenderResourceMap{},
        SceneMeshMaterialBindingFallbacks{});
    Require(binding.flags[0] == 1.0F, "KBMAT-0607: Mask alpha mode must be encoded for alpha-cutoff discard");
    Require(NearlyEqual(binding.params[3], material.alphaCutoff), "KBMAT-0607: Base/depth binding must pass alpha cutoff to the shader");

    const SceneMeshShadowMaterialBinding shadowBinding = SceneMeshMaterialBindingResolver::ResolveShadow(
        &material,
        RenderResourceRegistry{},
        SceneRenderResourceMap{},
        SceneMeshMaterialBindingFallbacks{});
    Require(shadowBinding.flags[0] == 1.0F, "KBMAT-0607: Shadow binding must preserve Mask alpha mode");
    Require(NearlyEqual(shadowBinding.params[3], material.alphaCutoff), "KBMAT-0607: Shadow binding must pass alpha cutoff to the shader");

    const SceneRenderMeshInstance instance{ .castsShadow = true };
    Require(MeshPipelinePassPolicy::Accepts(MeshPassType::Depth, instance, &material, {}), "KBMAT-0607: Mask material must remain accepted by the depth pass");
    Require(MeshPipelinePassPolicy::Accepts(MeshPassType::BaseOpaque, instance, &material, {}), "KBMAT-0607: Mask material must remain accepted by the base opaque pass");
    Require(MeshPipelinePassPolicy::Accepts(MeshPassType::ShadowDepth, instance, &material, {}), "KBMAT-0607: Mask material must remain accepted by the shadow pass");
}

void RunKbmat0608AlphaBlendSchemaReasonTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();
    const RenderMaterialParameterSchema* alphaMode = FindMaterialParameterSchema(schema, "alphaMode");
    Require(alphaMode != nullptr, "KBMAT-0608: alphaMode schema parameter is missing");
    Require(alphaMode->description.find("BLEND renders alpha-blended in the transparent pass") != std::string_view::npos,
            "KBMAT-MAT80: alphaMode schema must document that BLEND renders in the transparent pass");
}

void RunKbmat0609DoubleSidedSchemaTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();
    const RenderMaterialParameterSchema* doubleSided = FindMaterialParameterSchema(schema, "doubleSided");
    Require(doubleSided != nullptr, "KBMAT-0609: doubleSided schema parameter is missing");
    Require(doubleSided->type == RenderMaterialParameterType::Bool, "KBMAT-0609: doubleSided must be a bool parameter");
    Require(doubleSided->runtimeSupport == RenderMaterialFeatureSupport::Supported, "KBMAT-0609: doubleSided must be runtime-supported");
    Require(doubleSided->description.find("front and back faces") != std::string_view::npos,
            "KBMAT-0609: doubleSided schema must describe the cull policy");
}

void RunKbmat0609DoubleSidedRenderStateRuntimeTest() {
    RenderMeshResource singleSidedMesh{};
    singleSidedMesh.doubleSided = false;
    RenderMeshResource doubleSidedMesh{};
    doubleSidedMesh.doubleSided = true;
    RenderMaterialResource singleSidedMaterial{};
    singleSidedMaterial.doubleSided = false;
    RenderMaterialResource doubleSidedMaterial{};
    doubleSidedMaterial.doubleSided = true;

    const MeshPassType renderPasses[]{
        MeshPassType::Depth,
        MeshPassType::BaseOpaque,
        MeshPassType::ShadowDepth,
        MeshPassType::SelectionId,
        MeshPassType::EditorSelection,
        MeshPassType::Gizmo,
    };
    for (const MeshPassType pass : renderPasses) {
        const std::uint64_t singleSidedState = MeshPipelinePassPolicy::State(pass, &singleSidedMesh, &singleSidedMaterial);
        Require((singleSidedState & BGFX_STATE_CULL_CCW) != 0U, "KBMAT-0609: Single-sided mesh/material must cull back faces");
        Require((singleSidedState & BGFX_STATE_CULL_CW) == 0U, "KBMAT-0609: Single-sided mesh/material must not cull authored front faces");

        const std::uint64_t materialDoubleSidedState = MeshPipelinePassPolicy::State(pass, &singleSidedMesh, &doubleSidedMaterial);
        Require((materialDoubleSidedState & (BGFX_STATE_CULL_CW | BGFX_STATE_CULL_CCW)) == 0U,
                "KBMAT-0609: Material doubleSided must disable face culling");

        const std::uint64_t meshDoubleSidedState = MeshPipelinePassPolicy::State(pass, &doubleSidedMesh, &singleSidedMaterial);
        Require((meshDoubleSidedState & (BGFX_STATE_CULL_CW | BGFX_STATE_CULL_CCW)) == 0U,
                "KBMAT-0609: Mesh doubleSided must disable face culling");
    }
}

void RunMaterialPassPolicyAppliesMaterialRenderStateTest() {
    // Opaque material: writes depth, carries no blend bits (MAT-79).
    RenderMaterialResource opaque{};
    opaque.alphaMode = RenderMaterialAlphaMode::Opaque;
    const std::uint64_t opaqueState = MeshPipelinePassPolicy::State(MeshPassType::BaseOpaque, nullptr, &opaque);
    Require((opaqueState & BGFX_STATE_WRITE_Z) != 0U, "KBMAT-MAT79: opaque material must write depth");
    Require((opaqueState & BGFX_STATE_BLEND_MASK) == 0U, "KBMAT-MAT79: opaque material must not have a blend state");

    // Translucent alpha vs additive resolve to distinct bgfx blend states in the transparent pass.
    RenderMaterialResource alpha{};
    alpha.alphaMode = RenderMaterialAlphaMode::Blend;
    alpha.translucencyBlend = RenderMaterialTranslucencyBlend::Alpha;
    RenderMaterialResource additive{};
    additive.alphaMode = RenderMaterialAlphaMode::Blend;
    additive.translucencyBlend = RenderMaterialTranslucencyBlend::Additive;
    const std::uint64_t alphaState = MeshPipelinePassPolicy::State(MeshPassType::BaseTransparent, nullptr, &alpha);
    const std::uint64_t additiveState = MeshPipelinePassPolicy::State(MeshPassType::BaseTransparent, nullptr, &additive);
    Require((alphaState & BGFX_STATE_BLEND_MASK) != 0U, "KBMAT-MAT79: translucent material must receive a blend state");
    Require((alphaState & BGFX_STATE_BLEND_MASK) == (BGFX_STATE_BLEND_ALPHA & BGFX_STATE_BLEND_MASK),
        "KBMAT-MAT79: alpha translucency must map to bgfx alpha blend");
    Require((additiveState & BGFX_STATE_BLEND_MASK) == (BGFX_STATE_BLEND_ADD & BGFX_STATE_BLEND_MASK),
        "KBMAT-MAT79: additive translucency must map to bgfx additive blend");
    Require((alphaState & BGFX_STATE_BLEND_MASK) != (additiveState & BGFX_STATE_BLEND_MASK),
        "KBMAT-MAT79: distinct translucency blend modes must produce distinct render state");

    // MAT-38: every translucency blend mode resolves to its specific bgfx blend equation and all are distinct.
    const auto blendState = [](RenderMaterialTranslucencyBlend mode) {
        RenderMaterialResource material{};
        material.alphaMode = RenderMaterialAlphaMode::Blend;
        material.translucencyBlend = mode;
        return MeshPipelinePassPolicy::State(MeshPassType::BaseTransparent, nullptr, &material) & BGFX_STATE_BLEND_MASK;
    };
    const std::uint64_t modulateState = blendState(RenderMaterialTranslucencyBlend::Modulate);
    const std::uint64_t premultState = blendState(RenderMaterialTranslucencyBlend::PreMultipliedAlpha);
    const std::uint64_t holdoutState = blendState(RenderMaterialTranslucencyBlend::AlphaHoldout);
    Require(modulateState == (BGFX_STATE_BLEND_MULTIPLY & BGFX_STATE_BLEND_MASK), "KBMAT-MAT38: modulate must map to bgfx multiply blend");
    Require(premultState == (BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA) & BGFX_STATE_BLEND_MASK), "KBMAT-MAT38: premultiplied/AlphaComposite must map to one/inv-src-alpha");
    Require(holdoutState == (BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_ALPHA) & BGFX_STATE_BLEND_MASK), "KBMAT-MAT38: alpha holdout must map to zero/inv-src-alpha");
    const std::array<std::uint64_t, 5U> allStates{ alphaState & BGFX_STATE_BLEND_MASK, additiveState & BGFX_STATE_BLEND_MASK, modulateState, premultState, holdoutState };
    for (std::size_t i = 0U; i < allStates.size(); ++i) {
        for (std::size_t j = i + 1U; j < allStates.size(); ++j) {
            Require(allStates[i] != allStates[j], "KBMAT-MAT38: every blend mode must resolve to a distinct GPU blend state");
        }
    }

    // Depth-write control: opaque opting out drops WRITE_Z; default translucent never writes depth.
    RenderMaterialResource noDepthWrite{};
    noDepthWrite.writesDepth = false;
    const std::uint64_t noDepthState = MeshPipelinePassPolicy::State(MeshPassType::BaseOpaque, nullptr, &noDepthWrite);
    Require((noDepthState & BGFX_STATE_WRITE_Z) == 0U, "KBMAT-MAT79: a material opting out of depth write must not write Z");
    Require((alphaState & BGFX_STATE_WRITE_Z) == 0U, "KBMAT-MAT79: default translucent material must not write depth");
}

void RunMaterialTranslucencyBlendRoundTripTest() {
    // Parser: the new render-state fields are read into the desc (writer ordering is covered by the
    // canonical-ordering test in RenderResourceRegistryTests, which now includes both fields).
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "alphaMode MASK\n"
        "translucencyBlend ADDITIVE\n"
        "writesDepth false\n"
    };
    const std::optional<RenderMaterialAssetData> parsed = RenderMaterialAssetParser::Parse(input);
    Require(parsed.has_value(), "KBMAT-MAT79: material with render-state fields must parse");
    Require(parsed->desc.translucencyBlend == RenderMaterialTranslucencyBlend::Additive,
        "KBMAT-MAT79: translucencyBlend must parse into the material desc");
    Require(parsed->desc.writesDepth == false, "KBMAT-MAT79: writesDepth must parse into the material desc");

    // Writer emits the parsed render state back out (canonical round-trip of the new fields).
    std::ostringstream out;
    RenderMaterialAssetWriter::Write(out, *parsed);
    const std::string text = out.str();
    Require(text.find("translucencyBlend ADDITIVE") != std::string::npos, "KBMAT-MAT79: writer must emit translucencyBlend");
    Require(text.find("writesDepth false") != std::string::npos, "KBMAT-MAT79: writer must emit writesDepth");
}

void RunBuiltInPbrSchemaDistinguishesSupportedVsAdvancedTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();

    const RenderMaterialParameterSchema* baseColor = FindMaterialParameterSchema(schema, "baseColor");
    Require(baseColor != nullptr, "Schema missing baseColor");
    Require(baseColor->group == RenderMaterialParameterGroup::Core, "baseColor should be Core");
    Require(baseColor->runtimeSupport == RenderMaterialFeatureSupport::Supported, "baseColor should be Supported");

    const RenderMaterialParameterSchema* clearcoat = FindMaterialParameterSchema(schema, "clearcoatFactor");
    Require(clearcoat != nullptr, "Schema missing clearcoatFactor");
    Require(clearcoat->group == RenderMaterialParameterGroup::Advanced, "clearcoatFactor should be Advanced");
    Require(clearcoat->runtimeSupport == RenderMaterialFeatureSupport::ParsedButIgnored, "clearcoatFactor should be ParsedButIgnored");

    const RenderMaterialTextureSlotSchema* clearcoatTex = FindMaterialTextureSlotSchema(schema, "clearcoatTextureAssetId");
    Require(clearcoatTex != nullptr, "Schema missing clearcoat texture slot");
    Require(clearcoatTex->runtimeSupport == RenderMaterialFeatureSupport::ParsedButIgnored, "clearcoatTexture should be ParsedButIgnored");
}

void RunBuiltInPbrSchemaParserUsesSchemaForValidationTest() {
    // This test verifies that the parser now uses the schema for range validation
    std::istringstream input{
        "baseColor 1.2 0.5 0.5 1\n"
        "metallicFactor -0.5\n"
        "roughnessFactor -0.1\n"
        "normalScale 9\n"
        "emissiveStrength 65\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(!result.asset.has_value(), "Out-of-range values should fail parsing");
    Require(result.diagnostics.size() == 5U, "Should report 5 out-of-range diagnostics");
    Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::OutOfRange, "First diagnostic should be OutOfRange");
    Require(result.diagnostics[0].field == "baseColor", "First diagnostic should be for baseColor");
    Require(result.diagnostics[1].field == "metallicFactor", "Second diagnostic should be for metallicFactor");
    Require(result.diagnostics[2].field == "roughnessFactor", "Third diagnostic should be for roughnessFactor");
    Require(result.diagnostics[3].field == "normalScale", "Fourth diagnostic should be for normalScale");
    Require(result.diagnostics[4].field == "emissiveStrength", "Fifth diagnostic should be for emissiveStrength");
}

void RunBuiltInPbrSchemaParserUsesSchemaForUnsupportedFieldsTest() {
    std::istringstream input{
        "baseColor 0.25 0.5 0.75 1\n"
        "clearcoatFactor 0.5\n"
        "transmissionTexture Textures/transmission.kbtex\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "KBMAT-UE-0014: Asset should still be parsed with unsupported advanced PBR fields");
    Require(!result.Succeeded(), "KBMAT-UE-0014: Unsupported advanced PBR fields should keep warning diagnostics visible");
    Require(result.diagnostics.size() == 2U, "KBMAT-UE-0014: Should report 2 unsupported advanced PBR field diagnostics");
    Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField, "KBMAT-UE-0014: First diagnostic should be UnsupportedAdvancedField");
    Require(result.diagnostics[0].field == "clearcoatFactor", "KBMAT-UE-0014: First diagnostic should be for clearcoatFactor");
    Require(result.diagnostics[1].field == "transmissionTexture", "KBMAT-UE-0014: Second diagnostic should be for transmissionTexture");
}

[[nodiscard]] std::string ActiveUnsupportedParameterLine(const RenderMaterialParameterSchema& parameter) {
    std::string line{ parameter.name };
    line += ' ';
    if (parameter.name == "attenuationColor" || parameter.name == "subsurfaceColor") {
        line += "0.4 0.5 0.6";
    } else if (parameter.name == "decalBlendMode") {
        line += "PBR";
    } else if (parameter.name == "layerBlendMode") {
        line += "ADD";
    } else {
        switch (parameter.type) {
        case RenderMaterialParameterType::Scalar:
            line += parameter.name == "layerWeight" ? "0.5" : "0.5";
            break;
        case RenderMaterialParameterType::Vec3:
        case RenderMaterialParameterType::Color:
            line += "0.25 0.5 0.75";
            break;
        case RenderMaterialParameterType::Vec4:
            line += "0.25 0.5 0.75 1";
            break;
        case RenderMaterialParameterType::Enum:
            line += "ADD";
            break;
        case RenderMaterialParameterType::Bool:
            line += "true";
            break;
        case RenderMaterialParameterType::Texture:
            line += "123";
            break;
        }
    }
    line += '\n';
    return line;
}

[[nodiscard]] bool HasUnsupportedAdvancedWarning(
    const RenderMaterialAssetParseResult& result,
    std::string_view field) noexcept {
    for (const RenderMaterialAssetParseDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.code == RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField &&
            diagnostic.severity == RenderMaterialAssetParseDiagnosticSeverity::Warning &&
            diagnostic.field == field) {
            return true;
        }
    }
    return false;
}

void RunBuiltInPbrSchemaParserWarnsForEveryIgnoredAdvancedFieldTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();
    std::uint32_t checkedParameterCount = 0U;
    for (const RenderMaterialParameterSchema& parameter : schema.parameters) {
        if (parameter.runtimeSupport != RenderMaterialFeatureSupport::ParsedButIgnored) {
            continue;
        }
        std::istringstream input{
            "version 1\n"
            "materialType builtin.pbr\n"
            "materialTypeVersion 1\n"
            "baseColor 1 1 1 1\n" +
            ActiveUnsupportedParameterLine(parameter)
        };
        const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
        Require(result.asset.has_value(), "KBMAT-1001: Ignored advanced parameter should remain parseable");
        Require(HasUnsupportedAdvancedWarning(result, parameter.name), "KBMAT-1001: Ignored advanced parameter did not emit a warning diagnostic");
        ++checkedParameterCount;
    }
    Require(checkedParameterCount > 0U, "KBMAT-1001: Advanced parameter warning gate did not check any schema parameters");

    std::uint32_t checkedTextureSlotCount = 0U;
    for (const RenderMaterialTextureSlotSchema& slot : schema.textureSlots) {
        if (slot.runtimeSupport != RenderMaterialFeatureSupport::ParsedButIgnored) {
            continue;
        }
        {
            std::istringstream input{
                "version 1\n"
                "materialType builtin.pbr\n"
                "materialTypeVersion 1\n"
                "baseColor 1 1 1 1\n" +
                std::string{ slot.assetIdFieldName } + " 123\n"
            };
            const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
            Require(result.asset.has_value(), "KBMAT-1001: Ignored advanced texture asset field should remain parseable");
            Require(HasUnsupportedAdvancedWarning(result, slot.assetIdFieldName), "KBMAT-1001: Ignored advanced texture asset field did not emit a warning diagnostic");
        }
        {
            std::istringstream input{
                "version 1\n"
                "materialType builtin.pbr\n"
                "materialTypeVersion 1\n"
                "baseColor 1 1 1 1\n" +
                std::string{ slot.pathFieldName } + " Textures/ignored.kbtex\n"
            };
            const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
            Require(result.asset.has_value(), "KBMAT-1001: Ignored advanced texture path field should remain parseable");
            Require(HasUnsupportedAdvancedWarning(result, slot.pathFieldName), "KBMAT-1001: Ignored advanced texture path field did not emit a warning diagnostic");
        }
        ++checkedTextureSlotCount;
    }
    Require(checkedTextureSlotCount > 0U, "KBMAT-1001: Advanced texture warning gate did not check any schema texture slots");
}

void RunMaterialAssetAtomicSaveAndRoundTripTest() {
    // Build a material with explicit version, type, and a mix of core + advanced fields.
    RenderMaterialAssetData original{};
    original.documentVersion = 1;
    original.hasExplicitDocumentVersion = true;
    original.materialType = "builtin.pbr";
    original.hasExplicitMaterialType = true;
    original.materialTypeVersion = 1;
    original.hasExplicitMaterialTypeVersion = true;
    original.desc.baseColor[0] = 0.25F;
    original.desc.baseColor[1] = 0.5F;
    original.desc.baseColor[2] = 0.75F;
    original.desc.baseColor[3] = 1.0F;
    original.desc.metallicFactor = 0.3F;
    original.desc.roughnessFactor = 0.7F;
    original.desc.normalScale = 1.5F;
    original.desc.emissiveColor[0] = 0.1F;
    original.desc.emissiveColor[1] = 0.2F;
    original.desc.emissiveColor[2] = 0.3F;
    original.desc.emissiveStrength = 2.0F;
    original.desc.alphaMode = RenderMaterialAlphaMode::Mask;
    original.desc.alphaCutoff = 0.45F;
    original.desc.doubleSided = true;
    original.albedoTexturePath = "Textures/albedo.kbtex";
    original.normalTexturePath = "Textures/normal.kbtex";

    const std::filesystem::path tmpFile = std::filesystem::temp_directory_path() / "kbmat_atomic_test.kbmat";

    // Atomic save must succeed
    Require(RenderMaterialAssetWriter::Save(tmpFile, original), "Atomic save should succeed");

    // The temp file must NOT exist after successful rename
    Require(!std::filesystem::exists(tmpFile.string() + ".tmp"), "Temp file should not exist after atomic save");

    // Load back and verify round-trip. Assigned textures intentionally produce
    // TextureColorSpaceExpectation warnings (informational), so we only require
    // that no error-severity diagnostics are present.
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(tmpFile);
    Require(result.asset.has_value(), "Round-trip load should succeed");
    bool roundTripHasError = false;
    for (const RenderMaterialAssetParseDiagnostic& diag : result.diagnostics) {
        if (diag.severity == RenderMaterialAssetParseDiagnosticSeverity::Error) {
            roundTripHasError = true;
        }
    }
    Require(!roundTripHasError, "Round-trip load should have no error-severity diagnostics");

    const RenderMaterialAssetData& loaded = *result.asset;
    Require(loaded.documentVersion == original.documentVersion, "Round-trip documentVersion mismatch");
    Require(loaded.materialType == original.materialType, "Round-trip materialType mismatch");
    Require(loaded.materialTypeVersion == original.materialTypeVersion, "Round-trip materialTypeVersion mismatch");
    Require(NearlyEqual(loaded.desc.baseColor[0], original.desc.baseColor[0]), "Round-trip baseColor[0] mismatch");
    Require(NearlyEqual(loaded.desc.baseColor[1], original.desc.baseColor[1]), "Round-trip baseColor[1] mismatch");
    Require(NearlyEqual(loaded.desc.baseColor[2], original.desc.baseColor[2]), "Round-trip baseColor[2] mismatch");
    Require(NearlyEqual(loaded.desc.baseColor[3], original.desc.baseColor[3]), "Round-trip baseColor[3] mismatch");
    Require(NearlyEqual(loaded.desc.metallicFactor, original.desc.metallicFactor), "Round-trip metallicFactor mismatch");
    Require(NearlyEqual(loaded.desc.roughnessFactor, original.desc.roughnessFactor), "Round-trip roughnessFactor mismatch");
    Require(NearlyEqual(loaded.desc.normalScale, original.desc.normalScale), "Round-trip normalScale mismatch");
    Require(NearlyEqual(loaded.desc.emissiveColor[0], original.desc.emissiveColor[0]), "Round-trip emissiveColor[0] mismatch");
    Require(NearlyEqual(loaded.desc.emissiveColor[1], original.desc.emissiveColor[1]), "Round-trip emissiveColor[1] mismatch");
    Require(NearlyEqual(loaded.desc.emissiveColor[2], original.desc.emissiveColor[2]), "Round-trip emissiveColor[2] mismatch");
    Require(NearlyEqual(loaded.desc.emissiveStrength, original.desc.emissiveStrength), "Round-trip emissiveStrength mismatch");
    Require(loaded.desc.alphaMode == original.desc.alphaMode, "Round-trip alphaMode mismatch");
    Require(NearlyEqual(loaded.desc.alphaCutoff, original.desc.alphaCutoff), "Round-trip alphaCutoff mismatch");
    Require(loaded.desc.doubleSided == original.desc.doubleSided, "Round-trip doubleSided mismatch");
    Require(loaded.albedoTexturePath == original.albedoTexturePath, "Round-trip albedoTexturePath mismatch");
    Require(loaded.normalTexturePath == original.normalTexturePath, "Round-trip normalTexturePath mismatch");

    // Cleanup
    std::error_code ec;
    std::filesystem::remove(tmpFile, ec);
}

#if defined(_WIN32)
[[nodiscard]] bool HasMaterialWriterTempArtifacts(const std::filesystem::path& destination) {
    const std::string prefix = destination.filename().string() + ".tmp.";
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(destination.parent_path(), error)) {
        if (entry.path().filename().string().starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

void RunMaterialAtomicSaveFailurePreservesPreviousVersionTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "kb_material_atomic_failure_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    Require(!error, "P0.2: atomic failure test could not create its temp directory");

    const auto lockDestinationAgainstReplace = [](const std::filesystem::path& path) {
        return CreateFileW(
            path.wstring().c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
    };

    const std::filesystem::path materialPath = root / "Preserve.kbmat";
    RenderMaterialAssetData materialBefore{};
    materialBefore.desc.roughnessFactor = 0.2F;
    RenderMaterialAssetData materialAfter = materialBefore;
    materialAfter.desc.roughnessFactor = 0.9F;
    Require(RenderMaterialAssetWriter::Save(materialPath, materialBefore),
        "P0.2: atomic failure test could not seed a material");
    HANDLE materialLock = lockDestinationAgainstReplace(materialPath);
    Require(materialLock != INVALID_HANDLE_VALUE,
        "P0.2: atomic failure test could not lock the material destination");
    const bool materialSaveResult = RenderMaterialAssetWriter::Save(materialPath, materialAfter);
    CloseHandle(materialLock);
    Require(!materialSaveResult,
        "P0.2: material save unexpectedly reported success while atomic replacement was denied");
    const std::optional<RenderMaterialAssetData> preservedMaterial = RenderMaterialAssetLoader::LoadMaterial(materialPath);
    Require(preservedMaterial.has_value() && NearlyEqual(preservedMaterial->desc.roughnessFactor, 0.2F),
        "P0.2: failed material replacement changed or corrupted the previous file");
    Require(!HasMaterialWriterTempArtifacts(materialPath),
        "P0.2: failed material replacement leaked a temp file");

    const std::filesystem::path instancePath = root / "Preserve.kbmatinst";
    RenderMaterialInstanceAssetData instanceBefore{};
    instanceBefore.parentMaterialAssetId = kb::assets::AssetId{ 11U };
    RenderMaterialInstanceAssetData instanceAfter = instanceBefore;
    instanceAfter.parentMaterialAssetId = kb::assets::AssetId{ 22U };
    Require(RenderMaterialInstanceAssetWriter::Save(instancePath, instanceBefore),
        "P0.2: atomic failure test could not seed a material instance");
    HANDLE instanceLock = lockDestinationAgainstReplace(instancePath);
    Require(instanceLock != INVALID_HANDLE_VALUE,
        "P0.2: atomic failure test could not lock the material-instance destination");
    const bool instanceSaveResult = RenderMaterialInstanceAssetWriter::Save(instancePath, instanceAfter);
    CloseHandle(instanceLock);
    Require(!instanceSaveResult,
        "P0.2: material-instance save unexpectedly reported success while atomic replacement was denied");
    const std::optional<RenderMaterialInstanceAssetData> preservedInstance =
        RenderMaterialInstanceAssetLoader::LoadInstance(instancePath);
    Require(preservedInstance.has_value() && preservedInstance->parentMaterialAssetId == kb::assets::AssetId{ 11U },
        "P0.2: failed material-instance replacement changed or corrupted the previous file");
    Require(!HasMaterialWriterTempArtifacts(instancePath),
        "P0.2: failed material-instance replacement leaked a temp file");

    std::filesystem::remove_all(root, error);
}
#endif

void RunMaterialInstanceOverrideRoundTripAndValidatorTest() {
    RenderMaterialAssetData parent{};
    parent.materialType = kRenderMaterialAssetBuiltInPbrType;
    parent.materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
    parent.hasExplicitMaterialType = true;
    parent.hasExplicitMaterialTypeVersion = true;

    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = kb::assets::AssetId{ 777U };
    instance.hasOverrides = true;
    instance.overrides = parent;
    instance.overrides.desc.baseColor[0] = 0.12F;
    instance.overrides.desc.baseColor[1] = 0.34F;
    instance.overrides.desc.baseColor[2] = 0.56F;
    instance.overrides.desc.roughnessFactor = 0.42F;

    const RenderMaterialInstanceValidationResult valid = RenderMaterialInstanceAssetLoader::ValidateAgainstParent(instance, parent);
    Require(valid.Succeeded(), "Material instance validator rejected matching built-in PBR override");

    std::ostringstream output;
    RenderMaterialInstanceAssetWriter::Write(output, instance);
    Require(output.str().find("# KB material instance\nversion 1\nparentMaterialAssetId 777\nmaterialType builtin.pbr\nmaterialTypeVersion 1\nbaseColor") == 0U,
        "Material instance writer did not emit canonical instance header and override body");

    std::istringstream input{ output.str() };
    const RenderMaterialInstanceAssetParseResult loaded = RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(input);
    Require(loaded.asset.has_value(), "Material instance parser rejected writer output with override body");
    Require(loaded.asset->hasOverrides, "Material instance parser lost override body");
    Require(loaded.asset->parentMaterialAssetId.value == 777U, "Material instance parser lost parent material id");
    Require(loaded.asset->overrides.materialType == kRenderMaterialAssetBuiltInPbrType, "Material instance parser lost override material type");
    Require(loaded.asset->overrides.materialTypeVersion == kRenderMaterialAssetBuiltInPbrTypeVersion, "Material instance parser lost override material type version");
    Require(NearlyEqual(loaded.asset->overrides.desc.baseColor[0], 0.12F), "Material instance parser lost override base color");
    Require(NearlyEqual(loaded.asset->overrides.desc.roughnessFactor, 0.42F), "Material instance parser lost override roughness");

    RenderMaterialInstanceAssetData incompatibleType = instance;
    incompatibleType.overrides.materialType = "custom.graph";
    const RenderMaterialInstanceValidationResult typeValidation = RenderMaterialInstanceAssetLoader::ValidateAgainstParent(incompatibleType, parent);
    Require(!typeValidation.Succeeded() && typeValidation.diagnostics[0].code == RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialType,
        "Material instance validator did not reject incompatible material type");

    RenderMaterialInstanceAssetData incompatibleVersion = instance;
    incompatibleVersion.overrides.materialTypeVersion = parent.materialTypeVersion + 1U;
    const RenderMaterialInstanceValidationResult versionValidation = RenderMaterialInstanceAssetLoader::ValidateAgainstParent(incompatibleVersion, parent);
    Require(!versionValidation.Succeeded() && versionValidation.diagnostics[0].code == RenderMaterialInstanceValidationDiagnosticCode::IncompatibleMaterialTypeVersion,
        "Material instance validator did not reject incompatible material type version");

    RenderMaterialAssetData graphParent = parent;
    RenderMaterialGraphParameterValue parentBaseColor{};
    parentBaseColor.stableId = "baseColor";
    parentBaseColor.type = RenderMaterialParameterType::Color;
    parentBaseColor.numbers = { 1.0F, 1.0F, 1.0F, 1.0F };
    RenderMaterialGraphParameterValue parentRoughness{};
    parentRoughness.stableId = "roughnessFactor";
    parentRoughness.type = RenderMaterialParameterType::Scalar;
    parentRoughness.numbers[0] = 0.7F;
    graphParent.graphParameterValues.push_back(parentBaseColor);
    graphParent.graphParameterValues.push_back(parentRoughness);

    RenderMaterialInstanceAssetData graphInstance = instance;
    graphInstance.overrides = graphParent;
    graphInstance.overrides.graphParameterValues.clear();
    RenderMaterialGraphParameterValue roughnessOverride{};
    roughnessOverride.stableId = "roughnessFactor";
    roughnessOverride.type = RenderMaterialParameterType::Scalar;
    roughnessOverride.numbers[0] = 0.21F;
    graphInstance.overrides.graphParameterValues.push_back(roughnessOverride);
    const RenderMaterialInstanceValidationResult graphValid = RenderMaterialInstanceAssetLoader::ValidateAgainstParent(graphInstance, graphParent);
    Require(graphValid.Succeeded(), "Material instance validator rejected a graph parameter override exposed by the parent");

    RenderMaterialAssetData generatedStableIdParent = parent;
    generatedStableIdParent.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 42U,
        .kind = RenderMaterialGraphNodeKind::ParameterScalar,
    });
    RenderMaterialInstanceAssetData generatedStableIdInstance = instance;
    generatedStableIdInstance.overrides = generatedStableIdParent;
    generatedStableIdInstance.overrides.graphParameterValues.clear();
    RenderMaterialGraphParameterValue generatedStableIdOverride{};
    generatedStableIdOverride.stableId = "scalar42";
    generatedStableIdOverride.type = RenderMaterialParameterType::Scalar;
    generatedStableIdOverride.numbers[0] = 0.5F;
    generatedStableIdInstance.overrides.graphParameterValues.push_back(generatedStableIdOverride);
    const RenderMaterialInstanceValidationResult generatedStableIdValidation =
        RenderMaterialInstanceAssetLoader::ValidateAgainstParent(generatedStableIdInstance, generatedStableIdParent);
    Require(generatedStableIdValidation.Succeeded(),
        "Material instance validator rejected a parameter exposed through a generated graph stable id");

    RenderMaterialInstanceAssetData unknownParameter = graphInstance;
    unknownParameter.overrides.graphParameterValues.front().stableId = "roughnesFactor";
    const RenderMaterialInstanceValidationResult unknownParameterValidation = RenderMaterialInstanceAssetLoader::ValidateAgainstParent(unknownParameter, graphParent);
    Require(!unknownParameterValidation.Succeeded() &&
            unknownParameterValidation.diagnostics[0].code == RenderMaterialInstanceValidationDiagnosticCode::UnknownOverrideParameter,
        "Material instance validator did not reject an override parameter absent from the parent material");

    RenderMaterialInstanceAssetData wrongParameterType = graphInstance;
    wrongParameterType.overrides.graphParameterValues.front().type = RenderMaterialParameterType::Color;
    const RenderMaterialInstanceValidationResult wrongParameterTypeValidation = RenderMaterialInstanceAssetLoader::ValidateAgainstParent(wrongParameterType, graphParent);
    Require(!wrongParameterTypeValidation.Succeeded() &&
            wrongParameterTypeValidation.diagnostics[0].code == RenderMaterialInstanceValidationDiagnosticCode::IncompatibleOverrideParameterType,
        "Material instance validator did not reject a graph parameter override with the wrong type");
}

void RunMaterialInstanceStaticAndBaseOverrideTest() {
    const auto buildSwitchParent = [] {
        RenderMaterialAssetData parent{};
        parent.materialType = kRenderMaterialAssetBuiltInPbrType;
        parent.materialTypeVersion = kRenderMaterialAssetBuiltInPbrTypeVersion;
        parent.hasExplicitMaterialType = true;
        parent.hasExplicitMaterialTypeVersion = true;
        parent.graph = MakeDefaultRenderMaterialGraphDocument();
        parent.graph.nodes.push_back(RenderMaterialGraphNode{
            .id = 2U,
            .kind = RenderMaterialGraphNodeKind::StaticBoolParameter,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "useRed", .displayName = "Use Red", .defaultValueHint = "true" },
        });
        parent.graph.nodes.push_back(RenderMaterialGraphNode{
            .id = 3U,
            .kind = RenderMaterialGraphNodeKind::ConstantColor,
            .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" },
        });
        parent.graph.nodes.push_back(RenderMaterialGraphNode{
            .id = 4U,
            .kind = RenderMaterialGraphNodeKind::ConstantColor,
            .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 1 1" },
        });
        parent.graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::StaticSwitch });
        parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticBoolParameter, 2U, "value", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "value"));
        parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "true"));
        parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "false"));
        parent.graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticSwitch, 5U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return parent;
    };

    RenderMaterialAssetData parent = buildSwitchParent();
    RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = kb::assets::AssetId{ 1001U };
    instance.staticParameterOverrides.push_back(RenderMaterialInstanceStaticParameterOverride{
        .stableId = "useRed",
        .nodeKind = RenderMaterialGraphNodeKind::StaticBoolParameter,
        .value = "false",
    });
    instance.basePropertyOverrides.overrideBlendMode = true;
    instance.basePropertyOverrides.blendMode = RenderMaterialGraphBlendMode::Additive;
    instance.basePropertyOverrides.overrideShadingModel = true;
    instance.basePropertyOverrides.shadingModel = RenderMaterialShadingModel::Unlit;
    instance.basePropertyOverrides.overrideTwoSided = true;
    instance.basePropertyOverrides.twoSided = true;
    instance.basePropertyOverrides.overrideOpacityMaskClip = true;
    instance.basePropertyOverrides.opacityMaskClip = 0.25F;
    instance.basePropertyOverrides.overrideDomain = true;
    instance.basePropertyOverrides.domain = RenderMaterialDomain::Surface;

    const RenderMaterialInstanceValidationResult validation = RenderMaterialInstanceAssetLoader::ValidateAgainstParent(instance, parent);
    Require(validation.Succeeded(), "MAT-47 static/base material instance override should validate against the parent graph");

    const RenderMaterialAssetData effective = BuildEffectiveRenderMaterialInstanceAsset(parent, instance);
    Require(effective.graph.blendMode == "additive", "MAT-47 blendMode override did not update the effective graph blend mode");
    Require(effective.graph.shadingModel == "unlit", "MAT-47 shadingModel override did not update the effective graph shading model");
    Require(effective.graph.materialDomain == "surface", "MAT-47 domain override did not update the effective graph domain");
    Require(effective.desc.alphaMode == RenderMaterialAlphaMode::Blend &&
            effective.desc.translucencyBlend == RenderMaterialTranslucencyBlend::Additive,
        "MAT-47 blendMode override did not update the effective render state");
    Require(effective.desc.doubleSided, "MAT-47 twoSided override did not update the effective material state");
    Require(NearlyEqual(effective.desc.alphaCutoff, 0.25F), "MAT-47 opacityMaskClip override did not update alpha cutoff");

    const RenderMaterialGraphCompileResult parentCompile =
        CompileRenderMaterialGraphToShaderSource(parent.graph, RenderMaterialGraphBuildContext{ .assetId = 0x4700U });
    const RenderMaterialGraphCompileResult effectiveCompile =
        CompileRenderMaterialGraphToShaderSource(effective.graph, RenderMaterialGraphBuildContext{ .assetId = 0x4701U });
    Require(parentCompile.Succeeded() && effectiveCompile.Succeeded(), "MAT-47 parent and static-overridden instance graphs must compile");
    Require(parentCompile.shader.sourceHash != effectiveCompile.shader.sourceHash,
        "MAT-47 static override must produce a different graph shader variant key");
    Require(effectiveCompile.shader.source.find("vec4(0.0, 0.0, 1.0, 1.0)") != std::string::npos,
        "MAT-47 static override did not select the false/blue branch in generated shader source");
    Require(effectiveCompile.shader.source.find("material.alphaClipThreshold = 0.25") != std::string::npos,
        "MAT-47 opacityMaskClip override did not feed the graph alphaClipThreshold default");

    std::ostringstream output;
    RenderMaterialInstanceAssetWriter::Write(output, instance);
    const std::string text = output.str();
    Require(text.find("staticOverride useRed StaticBoolParameter false") != std::string::npos &&
            text.find("bOverride_blendMode true") != std::string::npos &&
            text.find("blendMode additive") != std::string::npos &&
            text.find("bOverride_twoSided true") != std::string::npos &&
            text.find("opacityMaskClip 0.25") != std::string::npos,
        "MAT-47 material instance writer did not serialize static/base override fields");

    std::istringstream input{ text };
    const RenderMaterialInstanceAssetParseResult parsed = RenderMaterialInstanceAssetLoader::LoadInstanceWithDiagnostics(input);
    Require(parsed.asset.has_value(), "MAT-47 material instance parser rejected static/base override fields");
    Require(parsed.asset->staticParameterOverrides.size() == 1U &&
            parsed.asset->basePropertyOverrides.overrideBlendMode &&
            parsed.asset->basePropertyOverrides.overrideOpacityMaskClip,
        "MAT-47 material instance parser lost static/base override fields");

    RenderMaterialInstanceAssetData wrongKind = instance;
    wrongKind.staticParameterOverrides.front().nodeKind = RenderMaterialGraphNodeKind::StaticComponentMask;
    const RenderMaterialInstanceValidationResult wrongKindValidation =
        RenderMaterialInstanceAssetLoader::ValidateAgainstParent(wrongKind, parent);
    Require(!wrongKindValidation.Succeeded() &&
            wrongKindValidation.diagnostics[0].code == RenderMaterialInstanceValidationDiagnosticCode::IncompatibleStaticOverrideParameterType,
        "MAT-47 validator did not reject a static override with the wrong node kind");
}

void RunBuiltInPbrSchemaAlphaModesListedTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();
    Require(schema.alphaModes.size() == 3U, "Schema should list 3 alpha modes");
    Require(schema.alphaModes[0] == "OPAQUE", "First alpha mode should be OPAQUE");
    Require(schema.alphaModes[1] == "MASK", "Second alpha mode should be MASK");
    Require(schema.alphaModes[2] == "BLEND", "Third alpha mode should be BLEND");
}

void RunBuiltInPbrSchemaUnsupportedAdvancedFeaturesListedTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();
    Require(!schema.unsupportedAdvancedFeatures.empty(), "Schema should list unsupported advanced features");
    bool foundClearcoat = false;
    bool foundTransmission = false;
    for (std::string_view feature : schema.unsupportedAdvancedFeatures) {
        if (feature == "clearcoatFactor") foundClearcoat = true;
        if (feature == "transmissionFactor") foundTransmission = true;
    }
    Require(foundClearcoat, "Schema should list clearcoatFactor as unsupported advanced");
    Require(foundTransmission, "Schema should list transmissionFactor as unsupported advanced");
}

void RunMaterialTypeMigrationTableAppliesLegacyFieldsTest() {
    const RenderMaterialTypeSchema& schema = GetBuiltInPbrMaterialTypeSchema();
    Require(FindMaterialTypeMigration(schema, RenderMaterialTypeMigrationOperationKind::RenameParameter, "baseColorFactor") != nullptr,
        "Material type migration table should rename baseColorFactor");
    Require(FindMaterialTypeMigration(schema, RenderMaterialTypeMigrationOperationKind::RenameParameter, "emissiveFactor") != nullptr,
        "Material type migration table should rename emissiveFactor");
    Require(FindMaterialTypeMigration(schema, RenderMaterialTypeMigrationOperationKind::SetDefault, "emissiveStrength") != nullptr,
        "Material type migration table should declare emissiveStrength default");
    Require(FindMaterialTypeMigration(schema, RenderMaterialTypeMigrationOperationKind::RemoveUnsupported, "specularGlossinessTexture") != nullptr,
        "Material type migration table should remove unsupported specular-glossiness texture");

    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColorFactor 0.2 0.4 0.6 1\n"
        "emissiveFactor 0.1 0.2 0.3\n"
        "specularGlossinessTexture Textures/specgloss.kbtex\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Material type migration table should keep legacy material parseable");
    Require(NearlyEqual(result.asset->desc.baseColor[0], 0.2F) && NearlyEqual(result.asset->desc.baseColor[2], 0.6F),
        "Material type migration rename did not apply baseColorFactor to baseColor");
    Require(NearlyEqual(result.asset->desc.emissiveColor[0], 0.1F) && NearlyEqual(result.asset->desc.emissiveColor[2], 0.3F),
        "Material type migration rename did not apply emissiveFactor to emissiveColor");
    Require(NearlyEqual(result.asset->desc.emissiveStrength, 1.0F), "Material type migration default did not preserve neutral emissiveStrength");
    Require(result.diagnostics.size() == 1U, "Material type migration remove should emit one warning");
    Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField &&
            result.diagnostics[0].severity == RenderMaterialAssetParseDiagnosticSeverity::Warning,
        "Material type migration remove should emit an unsupported advanced warning");
    Require(result.diagnostics[0].field == "specularGlossinessTexture", "Material type migration remove warning should identify the removed field");
}

void RunBuiltInPbrSchemaParserReportsTextureColorSpaceDiagnosticsTest() {
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 0.25 0.5 0.75 1\n"
        "albedoTextureAssetId 123\n"
        "normalTextureAssetId 456\n"
        "metallicRoughnessTextureAssetId 789\n"
        "occlusionTextureAssetId 101\n"
        "emissiveTextureAssetId 202\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Asset should parse successfully with texture slots");
    Require(!result.Succeeded(), "Should have warnings due to texture color space diagnostics");

    // Count TextureColorSpaceExpectation diagnostics
    std::size_t colorSpaceDiagnostics = 0;
    bool foundAlbedoSrgb = false;
    bool foundNormalLinear = false;
    bool foundMrLinear = false;
    bool foundOcclusionLinear = false;
    bool foundEmissiveSrgb = false;

    for (const auto& diag : result.diagnostics) {
        if (diag.code == RenderMaterialAssetParseDiagnosticCode::TextureColorSpaceExpectation) {
            ++colorSpaceDiagnostics;
            if (diag.field == "albedoTextureAssetId" && diag.message.find("sRGB") != std::string::npos) foundAlbedoSrgb = true;
            if (diag.field == "normalTextureAssetId" && diag.message.find("linear") != std::string::npos) foundNormalLinear = true;
            if (diag.field == "metallicRoughnessTextureAssetId" && diag.message.find("linear") != std::string::npos) foundMrLinear = true;
            if (diag.field == "occlusionTextureAssetId" && diag.message.find("linear") != std::string::npos) foundOcclusionLinear = true;
            if (diag.field == "emissiveTextureAssetId" && diag.message.find("sRGB") != std::string::npos) foundEmissiveSrgb = true;
        }
    }

    Require(colorSpaceDiagnostics == 5U, "Should report 5 texture color space diagnostics");
    Require(foundAlbedoSrgb, "Albedo texture should report sRGB expectation");
    Require(foundNormalLinear, "Normal texture should report linear expectation");
    Require(foundMrLinear, "Metallic-roughness texture should report linear expectation");
    Require(foundOcclusionLinear, "Occlusion texture should report linear expectation");
    Require(foundEmissiveSrgb, "Emissive texture should report sRGB expectation");
}

void RunBuiltInPbrSchemaParserTextureColorSpaceDiagnosticsOnlyForAssignedTexturesTest() {
    // Material with no texture assignments should not produce color space diagnostics
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 0.25 0.5 0.75 1\n"
        "metallicFactor 0.5\n"
        "roughnessFactor 0.5\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Asset should parse successfully");
    Require(result.Succeeded(), "No texture assignments means no color space diagnostics");
    Require(result.diagnostics.empty(), "Should have no diagnostics at all");
}

void RunBuiltInPbrSchemaParserTextureColorSpaceDiagnosticsForPathFieldsTest() {
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 0.25 0.5 0.75 1\n"
        "albedoTexture Textures/albedo.kbtex\n"
        "normalTexture Textures/normal.kbtex\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Asset should parse successfully with texture path fields");
    Require(!result.Succeeded(), "Should have warnings due to texture color space diagnostics");

    std::size_t colorSpaceDiagnostics = 0;
    bool foundAlbedoPath = false;
    bool foundNormalPath = false;

    for (const auto& diag : result.diagnostics) {
        if (diag.code == RenderMaterialAssetParseDiagnosticCode::TextureColorSpaceExpectation) {
            ++colorSpaceDiagnostics;
            if (diag.field == "albedoTexture") foundAlbedoPath = true;
            if (diag.field == "normalTexture") foundNormalPath = true;
        }
    }

    Require(colorSpaceDiagnostics == 2U, "Should report 2 texture color space diagnostics for path fields");
    Require(foundAlbedoPath, "Albedo texture path should report color space diagnostic");
    Require(foundNormalPath, "Normal texture path should report color space diagnostic");
}

void RunMaterialAssetTilingOffsetRoundTripTest() {
    // KBMAT-0108: per-material tiling and offset round-trip through the .kbmat format.
    RenderMaterialAssetData original{};
    original.desc.uvTiling[0] = 2.5F;
    original.desc.uvTiling[1] = 0.5F;
    original.desc.uvOffset[0] = 0.25F;
    original.desc.uvOffset[1] = -1.0F;

    std::ostringstream output;
    RenderMaterialAssetWriter::Write(output, original);
    Require(output.str().find("tiling 2.5 0.5\n") != std::string::npos, "Material writer did not emit tiling");
    Require(output.str().find("offset 0.25 -1\n") != std::string::npos, "Material writer did not emit offset");

    std::istringstream input{ output.str() };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Tiling/offset material should parse");
    Require(result.Succeeded(), "Tiling/offset material should have no diagnostics");
    Require(NearlyEqual(result.asset->desc.uvTiling[0], 2.5F), "Round-trip lost uvTiling[0]");
    Require(NearlyEqual(result.asset->desc.uvTiling[1], 0.5F), "Round-trip lost uvTiling[1]");
    Require(NearlyEqual(result.asset->desc.uvOffset[0], 0.25F), "Round-trip lost uvOffset[0]");
    Require(NearlyEqual(result.asset->desc.uvOffset[1], -1.0F), "Round-trip lost uvOffset[1]");
}

void RunMaterialGraphRoundTripTest() {
    RenderMaterialAssetData original{};
    original.graph = MakeDefaultRenderMaterialGraphDocument();
    original.graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 240,
        .positionY = 180,
        .parameter = RenderMaterialGraphParameterMetadata{
            .displayName = "Warm Tint",
            .defaultValueHint = "0.2 0.4 0.6 1",
            .hasRange = true,
            .rangeMin = 0.0F,
            .rangeMax = 1.0F,
            .overrideSupported = false,
        },
    });
    RenderMaterialGraphLink baseColorLink{
        .fromNodeId = 2U,
        .fromPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::ConstantColor, "rgba", true),
        .fromPin = "rgba",
        .toNodeId = 1U,
        .toPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
        .toPin = "baseColor",
    };
    baseColorLink.id = MakeRenderMaterialGraphLinkId(baseColorLink);
    original.graph.links.push_back(baseColorLink);
    original.graph.comments.push_back(RenderMaterialGraphCommentBox{
        .id = 7U,
        .positionX = 200,
        .positionY = 120,
        .width = 360,
        .height = 180,
        .color = 0x4A6385U,
        .text = "Surface #note",
    });

    std::ostringstream output;
    RenderMaterialAssetWriter::Write(output, original);
    Require(output.str().find("graphVersion 2\n") != std::string::npos, "Material writer did not emit graph version");
    Require(output.str().find("graphMaterialDomain surface\n") != std::string::npos, "Material writer did not emit graph material domain");
    Require(output.str().find("graphShadingModel defaultLit\n") != std::string::npos, "Material writer did not emit graph shading model");
    Require(output.str().find("graphStorageModel inline-kbmat\n") != std::string::npos, "Material writer did not emit inline graph storage model");
    Require(output.str().find("graphDiagnosticSchemaVersion 1\n") != std::string::npos, "Material writer did not emit graph diagnostic schema version");
    Require(output.str().find("graphPersistCompileDiagnostics true\n") != std::string::npos, "Material writer did not emit graph diagnostics persistence policy");
    Require(output.str().find("graphArtifactFailurePolicy LastGoodThenErrorMaterial\n") != std::string::npos, "Material writer did not emit graph artifact failure policy");
    Require(output.str().find("graphNode 1 MaterialOutput 640 240\n") != std::string::npos, "Material writer did not emit material output node");
    Require(output.str().find("graphNode 2 ConstantColor 240 180\n") != std::string::npos, "Material writer did not emit constant color node");
    Require(output.str().find("graphParameter 2 _ Warm%20Tint Core 0.2%200.4%200.6%201 0.000000 1.000000 _ Unknown false 0 _\n") != std::string::npos,
        "Material writer did not emit constant color value metadata");
    const std::string expectedLink = "graphLink " + std::to_string(baseColorLink.id) + " 2 " + std::to_string(baseColorLink.fromPinId) + " rgba 1 " + std::to_string(baseColorLink.toPinId) + " baseColor\n";
    Require(output.str().find(expectedLink) != std::string::npos, "Material writer did not emit graph link with stable ids");
    Require(output.str().find("graphComment 7 200 120 360 180 4875141 Surface%20%23note\n") != std::string::npos,
        "Material writer did not emit graph comment box with encoded text");

    std::istringstream input{ output.str() };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Material graph round-trip should parse");
    Require(result.Succeeded(), "Material graph round-trip should have no diagnostics");
    Require(result.asset->graph.hasExplicitDocumentVersion, "Material graph round-trip lost explicit graph version");
    Require(result.asset->graph.materialDomain == "surface", "Material graph round-trip lost graph material domain");
    Require(result.asset->graph.shadingModel == "defaultLit", "Material graph round-trip lost graph shading model");
    Require(result.asset->graph.storageModel == "inline-kbmat", "Material graph round-trip lost inline storage decision");
    Require(result.asset->graph.diagnosticSchemaVersion == 1U && result.asset->graph.persistCompileDiagnostics, "Material graph round-trip lost diagnostic metadata");
    Require(result.asset->graph.hasExplicitArtifactFailurePolicy, "Material graph round-trip lost explicit artifact failure policy");
    Require(result.asset->graph.artifactFailurePolicy == RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial, "Material graph round-trip changed artifact failure policy");
    Require(result.asset->graph.nodes.size() == 2U, "Material graph round-trip lost nodes");
    Require(result.asset->graph.links.size() == 1U, "Material graph round-trip lost links");
    Require(result.asset->graph.comments.size() == 1U, "Material graph round-trip lost comment boxes");
    Require(result.asset->graph.nodes[0].kind == RenderMaterialGraphNodeKind::MaterialOutput, "Material graph round-trip changed output node kind");
    Require(result.asset->graph.nodes[1].kind == RenderMaterialGraphNodeKind::ConstantColor, "Material graph round-trip changed constant node kind");
    Require(result.asset->graph.nodes[1].parameter.displayName == "Warm Tint" &&
            result.asset->graph.nodes[1].parameter.defaultValueHint == "0.2 0.4 0.6 1" &&
            result.asset->graph.nodes[1].parameter.hasRange &&
            !result.asset->graph.nodes[1].parameter.overrideSupported,
        "Material graph round-trip lost constant color value metadata");
    Require(result.asset->graph.links[0].fromNodeId == 2U && result.asset->graph.links[0].toNodeId == 1U, "Material graph round-trip changed link nodes");
    Require(result.asset->graph.links[0].id == baseColorLink.id && result.asset->graph.links[0].fromPinId == baseColorLink.fromPinId && result.asset->graph.links[0].toPinId == baseColorLink.toPinId, "Material graph round-trip changed stable link identity");
    Require(result.asset->graph.links[0].fromPin == "rgba" && result.asset->graph.links[0].toPin == "baseColor", "Material graph round-trip changed link pins");
    Require(result.asset->graph.comments[0].id == 7U &&
            result.asset->graph.comments[0].positionX == 200 &&
            result.asset->graph.comments[0].positionY == 120 &&
            result.asset->graph.comments[0].width == 360 &&
            result.asset->graph.comments[0].height == 180 &&
            result.asset->graph.comments[0].color == 0x4A6385U &&
            result.asset->graph.comments[0].text == "Surface #note",
        "Material graph round-trip changed comment box metadata");
}

void RunMaterialGraphSchemaMigrationGoldenTest() {
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 1 1 1 1\n"
        "graphVersion 1\n"
        "graphMaterialDomain surface\n"
        "graphShadingModel lit\n"
        "graphBlendMode opaque\n"
        "graphStorageModel inline-kbmat\n"
        "graphDiagnosticSchemaVersion 1\n"
        "graphPersistCompileDiagnostics true\n"
        "graphArtifactFailurePolicy LastGoodThenErrorMaterial\n"
        "graphNode 1 MaterialOutput 640 240\n"
        "graphNode 2 Color 120 80\n"
        "graphParameter 2 legacyTint Legacy%20Tint Core 0.25%200.5%200.75%201 0 1 _ Unknown false 4 Legacy%20color\n"
        "graphLink 2 rgba 1 baseColor\n"
    };

    const RenderMaterialAssetParseResult migrated = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(migrated.asset.has_value(), "MAT-70: v1 graph asset should remain loadable after migration");
    Require(!migrated.Succeeded(), "MAT-70: migrated deprecated graph schema should keep a visible warning diagnostic");
    const RenderMaterialAssetParseDiagnostic* migration = FindParseDiagnostic(migrated.diagnostics, RenderMaterialAssetParseDiagnosticCode::GraphMigration);
    Require(migration != nullptr, "MAT-70: v1 graph migration should emit a typed migration diagnostic");
    Require(migration->severity == RenderMaterialAssetParseDiagnosticSeverity::Warning, "MAT-70: graph migration diagnostic should be a warning");
    Require(migration->field == "graphShadingModel" && migration->line == 7U, "MAT-70: graph migration diagnostic should identify the deprecated field line");
    Require(migration->message.find("lit") != std::string::npos && migration->message.find("defaultLit") != std::string::npos,
        "MAT-70: graph migration diagnostic should name the deprecated and canonical shading tokens");
    Require(RenderMaterialAssetParseDiagnosticCodeName(RenderMaterialAssetParseDiagnosticCode::GraphMigration) == std::string_view{ "graph_migration" },
        "MAT-70: graph migration diagnostic code should have a stable serialized name");

    const RenderMaterialAssetData& asset = *migrated.asset;
    Require(asset.graph.documentVersion == kRenderMaterialGraphDocumentVersion && asset.graph.hasExplicitDocumentVersion,
        "MAT-70: migrated graph should be upgraded to the current schema version");
    Require(asset.graph.shadingModel == "defaultLit", "MAT-70: migrated graph should canonicalize lit to defaultLit");
    Require(asset.graph.nodes.size() == 2U && asset.graph.links.size() == 1U, "MAT-70: graph migration should preserve nodes and links");
    Require(asset.graph.nodes[1].kind == RenderMaterialGraphNodeKind::ConstantColor, "MAT-70: graph migration should keep the parsed node kind");
    Require(asset.graph.nodes[1].parameter.stableId == "legacyTint" &&
            asset.graph.nodes[1].parameter.displayName == "Legacy Tint" &&
            asset.graph.nodes[1].parameter.defaultValueHint == "0.25 0.5 0.75 1" &&
            !asset.graph.nodes[1].parameter.overrideSupported,
        "MAT-70: graph migration should preserve parameter metadata without data loss");

    const RenderMaterialGraphCompileResult compile = CompileRenderMaterialGraphToShaderSource(
        asset.graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0470U, .sourcePath = "/Game/Materials/MigratedV1.kbmat" });
    Require(compile.Succeeded(), "MAT-70: migrated v1 graph should compile after schema upgrade");

    std::ostringstream canonicalOutput;
    RenderMaterialAssetWriter::Write(canonicalOutput, asset);
    const std::string canonical = canonicalOutput.str();
    Require(canonical.find("graphVersion 2\n") != std::string::npos, "MAT-70: canonical writer should emit the current graph schema version");
    Require(canonical.find("graphShadingModel defaultLit\n") != std::string::npos, "MAT-70: canonical writer should emit the canonical shading model token");
    Require(canonical.find("graphShadingModel lit\n") == std::string::npos, "MAT-70: canonical writer should not preserve deprecated shading tokens");
    Require(canonical.find("graphNode 2 ConstantColor 120 80\n") != std::string::npos, "MAT-70: canonical writer should preserve migrated node data");

    std::istringstream canonicalInput{ canonical };
    const RenderMaterialAssetParseResult reparsed = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(canonicalInput);
    Require(reparsed.Succeeded() && reparsed.asset.has_value(), "MAT-70: canonical migrated output should parse without migration diagnostics");
    Require(reparsed.asset->graph.documentVersion == kRenderMaterialGraphDocumentVersion &&
            reparsed.asset->graph.shadingModel == "defaultLit" &&
            reparsed.asset->graph.nodes.size() == asset.graph.nodes.size() &&
            reparsed.asset->graph.links.size() == asset.graph.links.size(),
        "MAT-70: canonical migration golden should round-trip without graph data loss");
}

void RunMaterialGraphStableLinkIdMigrationTest() {
    const std::uint32_t fromPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::TextureSample, "color", true);
    const std::uint32_t toPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false);
    RenderMaterialGraphLink expectedLink{
        .fromNodeId = 2U,
        .fromPinId = fromPinId,
        .toNodeId = 1U,
        .toPinId = toPinId,
    };
    const std::uint32_t expectedLinkId = MakeRenderMaterialGraphLinkId(expectedLink);
    const std::uint32_t staleLinkId = expectedLinkId == 123U ? 124U : 123U;
    std::ostringstream text;
    text << "version 1\n"
         << "materialType builtin.pbr\n"
         << "materialTypeVersion 1\n"
         << "baseColor 1 1 1 1\n"
         << "graphVersion 1\n"
         << "graphNode 1 MaterialOutput 640 240\n"
         << "graphNode 2 TextureSample 240 180\n"
         << "graphLink " << staleLinkId << " 2 " << fromPinId << " color 1 " << toPinId << " baseColor\n";

    std::istringstream input{ text.str() };
    const RenderMaterialAssetParseResult migrated = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(migrated.asset.has_value(), "Material graph stable link id migration should keep the asset loadable");
    Require(!migrated.Succeeded(), "Material graph stable link id migration should keep a visible warning diagnostic");
    const RenderMaterialAssetParseDiagnostic* migration = FindParseDiagnostic(migrated.diagnostics, RenderMaterialAssetParseDiagnosticCode::GraphMigration);
    Require(migration != nullptr && migration->field == "graphLink" && migration->severity == RenderMaterialAssetParseDiagnosticSeverity::Warning,
        "Material graph stable link id migration should emit a graphLink warning");
    Require(migrated.asset->graph.links.size() == 1U &&
            migrated.asset->graph.links[0].id == expectedLinkId &&
            migrated.asset->graph.links[0].fromPinId == fromPinId &&
            migrated.asset->graph.links[0].toPinId == toPinId,
        "Material graph stable link id migration should normalize the link identity");
}

void RunMaterialGraphMultiWordNodeKindSerializationRoundTripTest() {
    // Regression: RenderMaterialGraphNodeKindName is used both for serialization (graphNode <id> <kind> ...)
    // and the parser reads the kind as a SINGLE whitespace token. Every multi-word node kind therefore has
    // to serialize as a single token or it silently collapses / fails on reload. These kinds all used to
    // emit spaced names ("Vertex Color", "Camera Vector", "Scene Depth", "Texture Coordinate", ...) and could
    // not round-trip; SrgbToLinear/Exponential2 are the newest math/color nodes. Assert the whole set survives.
    const RenderMaterialGraphNodeKind kinds[] = {
        RenderMaterialGraphNodeKind::VertexColor,
        RenderMaterialGraphNodeKind::CameraVector,
        RenderMaterialGraphNodeKind::SceneDepth,
        RenderMaterialGraphNodeKind::TextureCoordinate,
        RenderMaterialGraphNodeKind::MakeMaterialAttributes,
        RenderMaterialGraphNodeKind::SrgbToLinear,
        RenderMaterialGraphNodeKind::Exponential2,
    };

    RenderMaterialAssetData original{};
    original.graph = MakeDefaultRenderMaterialGraphDocument();
    std::uint32_t nextId = 2U;
    for (const RenderMaterialGraphNodeKind kind : kinds) {
        original.graph.nodes.push_back(RenderMaterialGraphNode{
            .id = nextId,
            .kind = kind,
            .positionX = static_cast<int>(120 + nextId * 40U),
            .positionY = 200,
        });
        ++nextId;
    }

    std::ostringstream output;
    RenderMaterialAssetWriter::Write(output, original);
    // Serialized kind tokens must contain no spaces (a spaced kind would be truncated by the parser).
    Require(output.str().find("graphNode 2 VertexColor ") != std::string::npos, "VertexColor must serialize as a single token");
    Require(output.str().find("graphNode 3 CameraVector ") != std::string::npos, "CameraVector must serialize as a single token");
    Require(output.str().find("graphNode 4 SceneDepth ") != std::string::npos, "SceneDepth must serialize as a single token");
    Require(output.str().find("graphNode 5 TextureCoordinate ") != std::string::npos, "TextureCoordinate must serialize as a single token");
    Require(output.str().find("graphNode 7 SrgbToLinear ") != std::string::npos, "SrgbToLinear must serialize as a single token");
    Require(output.str().find("graphNode 8 Exponential2 ") != std::string::npos, "Exponential2 must serialize as a single token");

    std::istringstream input{ output.str() };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Multi-word node graph should parse");
    Require(result.Succeeded(), "Multi-word node graph round-trip should have no diagnostics");
    Require(result.asset->graph.nodes.size() == std::size(kinds) + 1U, "Multi-word node round-trip lost nodes");
    for (std::size_t i = 0U; i < std::size(kinds); ++i) {
        Require(result.asset->graph.nodes[i + 1U].kind == kinds[i], "Multi-word node round-trip changed a node kind");
    }
}

void RunMaterialGraphEveryShaderNodeKindHasCodegenTest() {
    // MAT-50 gate: every graph node kind must lower to real shader code (no missing codegen, no fallback).
    // Route each value-producing node's first output into a type-compatible MaterialOutput input and require
    // the graph to validate on declared render paths and compile across Forward/Forward+/Deferred contexts.
    // Texture outputs are fed through a TextureSample.
    for (const RenderMaterialGraphNodeKind kind : AllRenderMaterialGraphNodeKinds()) {
        if (kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            continue;
        }
        if (kind == RenderMaterialGraphNodeKind::FunctionInput ||
            kind == RenderMaterialGraphNodeKind::FunctionOutput ||
            kind == RenderMaterialGraphNodeKind::MaterialFunctionCall ||
            kind == RenderMaterialGraphNodeKind::LayerStack) {
            continue;
        }
        const std::vector<std::string> outputs = RenderMaterialGraphNodeOutputPinNames(kind);
        if (outputs.empty()) {
            continue;  // a pure sink with no value to route
        }
        const std::string outPin = outputs.front();
        const RenderMaterialGraphPinType outType = RenderMaterialGraphPinDataType(kind, outPin, true);
        const std::string kindName{ RenderMaterialGraphNodeKindName(kind) };

        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        RenderMaterialGraphNode subject{ .id = 2U, .kind = kind };
        if (kind == RenderMaterialGraphNodeKind::Reroute ||
            kind == RenderMaterialGraphNodeKind::CompositeInput ||
            kind == RenderMaterialGraphNodeKind::CompositeOutput) {
            subject.parameter.defaultValueHint = std::string{ RenderMaterialGraphPinTypeName(outType) };
        } else if (kind == RenderMaterialGraphNodeKind::CollectionParameter) {
            subject.parameter.stableId = "mat50CollectionValue";
            subject.parameter.displayName = "MAT50 Collection Value";
            subject.parameter.defaultValueHint = "37150";
        } else if (kind == RenderMaterialGraphNodeKind::NamedRerouteUsage) {
            subject.parameter.stableId = "mat50Route";
            subject.parameter.displayName = "MAT50 Route";
            subject.parameter.defaultValueHint = std::string{ RenderMaterialGraphPinTypeName(outType) };
        }
        if (outType == RenderMaterialGraphPinType::Texture2D ||
            outType == RenderMaterialGraphPinType::TextureCube ||
            outType == RenderMaterialGraphPinType::Texture3D ||
            outType == RenderMaterialGraphPinType::Texture2DArray) {
            subject.parameter.textureRole = "baseColor";  // a texture parameter must declare its role
        }
        graph.nodes.push_back(subject);
        if (kind == RenderMaterialGraphNodeKind::Reroute ||
            kind == RenderMaterialGraphNodeKind::CompositeInput ||
            kind == RenderMaterialGraphNodeKind::CompositeOutput) {
            RenderMaterialGraphNode source{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
            source.parameter.defaultValueHint = "0.4 0.5 0.6 1";
            graph.nodes.push_back(source);
            graph.links.push_back(MakeGraphLink(source, "rgba", subject, "input"));
        } else if (kind == RenderMaterialGraphNodeKind::NamedRerouteUsage) {
            RenderMaterialGraphNode declaration{ .id = 3U, .kind = RenderMaterialGraphNodeKind::NamedRerouteDeclaration };
            declaration.parameter.stableId = "mat50Route";
            declaration.parameter.displayName = "MAT50 Route";
            declaration.parameter.defaultValueHint = std::string{ RenderMaterialGraphPinTypeName(outType) };
            RenderMaterialGraphNode source{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ConstantColor };
            source.parameter.defaultValueHint = "0.4 0.5 0.6 1";
            graph.nodes.push_back(declaration);
            graph.nodes.push_back(source);
            graph.links.push_back(MakeGraphLink(source, "rgba", declaration, "input"));
        }
        // Route the node's output into a MaterialOutput input whose type matches, so the compile exercises the
        // node's codegen without tripping the link type validator. Texture/UV outputs feed a TextureSample.
        switch (outType) {
        case RenderMaterialGraphPinType::Texture2D:
            graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample });
            graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::TextureSample, 3U, "texture"));
            graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U,
                RenderMaterialGraphNodeOutputPinNames(RenderMaterialGraphNodeKind::TextureSample).front(),
                RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
            break;
        case RenderMaterialGraphPinType::TextureCube:
            graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSampleCube });
            graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::TextureSampleCube, 3U, "texture"));
            graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSampleCube, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
            break;
        case RenderMaterialGraphPinType::Texture3D:
            graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSampleVolume });
            graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::TextureSampleVolume, 3U, "texture"));
            graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSampleVolume, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
            break;
        case RenderMaterialGraphPinType::Texture2DArray:
            graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample2DArray });
            graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::TextureSample2DArray, 3U, "texture"));
            graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample2DArray, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
            break;
        case RenderMaterialGraphPinType::Float2:
            graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample });
            graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::TextureSample, 3U, "uv"));
            graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U,
                RenderMaterialGraphNodeOutputPinNames(RenderMaterialGraphNodeKind::TextureSample).front(),
                RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
            break;
        case RenderMaterialGraphPinType::MaterialAttributes:
            graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::MaterialOutput, 1U, "attributes"));
            break;
        case RenderMaterialGraphPinType::Float:
            graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
            break;
        case RenderMaterialGraphPinType::Normal:
            graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::MaterialOutput, 1U, "normal"));
            break;
        case RenderMaterialGraphPinType::Float3:
            graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));
            break;
        case RenderMaterialGraphPinType::Float4:
        case RenderMaterialGraphPinType::Color:
        case RenderMaterialGraphPinType::Unknown:
        default:
            graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
            break;
        }

        const auto requirePathValidation = [&](RenderMaterialGraphRenderPath path, const char* pathName) {
            const std::vector<RenderMaterialGraphDiagnostic> diagnostics = ValidateRenderMaterialGraphDocument(graph, path);
            const bool unsupported = HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode);
            bool error = false;
            for (const RenderMaterialGraphDiagnostic& diagnostic : diagnostics) {
                error = error || diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
            }

            const RenderMaterialGraphNodeSupport pathSupport = RenderMaterialGraphNodeSupportForPath(kind, path);
            if (pathSupport == RenderMaterialGraphNodeSupport::Unsupported) {
                Require(unsupported,
                    ("KBMAT-MAT50: node kind '" + kindName + "' must fail closed on unsupported " + pathName + " validation").c_str());
                return;
            }

            std::string failure = "KBMAT-MAT50: node kind '" + kindName + "' must validate on " + pathName;
            if (error && !diagnostics.empty()) {
                failure += " (first diagnostic: " + diagnostics.front().message + ")";
            }
            Require(!error, failure.c_str());
        };

        requirePathValidation(RenderMaterialGraphRenderPath::GpuForward, "GpuForward");
        requirePathValidation(RenderMaterialGraphRenderPath::Preview, "Preview");
        requirePathValidation(RenderMaterialGraphRenderPath::GpuDeferred, "GpuDeferred");

        struct CompilePathCase {
            RenderMaterialGraphShadingPath shadingPath;
            std::uint32_t assetSalt;
            const char* name;
            bool required;
        };
        const bool deferredRequired =
            RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::GpuDeferred) != RenderMaterialGraphNodeSupport::Unsupported;
        const std::array<CompilePathCase, 3U> compilePaths{ {
            CompilePathCase{ .shadingPath = RenderMaterialGraphShadingPath::Forward, .assetSalt = 0x0000U, .name = "Forward", .required = true },
            CompilePathCase{ .shadingPath = RenderMaterialGraphShadingPath::ForwardPlus, .assetSalt = 0x1000U, .name = "Forward+", .required = true },
            CompilePathCase{ .shadingPath = RenderMaterialGraphShadingPath::Deferred, .assetSalt = 0x2000U, .name = "Deferred", .required = deferredRequired },
        } };

        for (const CompilePathCase& compilePath : compilePaths) {
            if (!compilePath.required) {
                continue;
            }
            const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
                graph,
                RenderMaterialGraphBuildContext{
                    .assetId = 0x9000U + compilePath.assetSalt + static_cast<std::uint32_t>(kind),
                    .shadingPath = compilePath.shadingPath,
                });
            std::string failure = "KBMAT-MAT50: node kind '" + kindName + "' must lower to " + compilePath.name + " shader code with no diagnostics";
            if (!compiled.Succeeded() && !compiled.diagnostics.empty()) {
                failure += " (first diagnostic: " + compiled.diagnostics.front().message + ")";
            }
            Require(compiled.Succeeded(), failure.c_str());
            Require(!compiled.shader.source.empty(),
                ("KBMAT-MAT50: node kind '" + kindName + "' must emit non-empty " + compilePath.name + " shader source").c_str());
        }
    }
}

void RunMaterialGraphDefaultsLegacyMaterialToOutputNodeTest() {
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 1 1 1 1\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Legacy material without graph fields should still parse");
    Require(result.Succeeded(), "Legacy material graph default should not produce diagnostics");
    Require(result.asset->graph.nodes.size() == 1U, "Legacy material should receive a default graph node");
    Require(result.asset->graph.nodes[0].kind == RenderMaterialGraphNodeKind::MaterialOutput, "Legacy material default graph should contain Material Output");
}

void RunMaterialGraphLastGoodArtifactPolicyRoundTripAndDecisionTest() {
    RenderMaterialAssetData original{};
    original.graph = MakeDefaultRenderMaterialGraphDocument();
    original.graph.lastGoodArtifact.assetId = 12001U;
    original.graph.lastGoodArtifact.contentHash = 0xCAFE1234U;

    std::ostringstream output;
    RenderMaterialAssetWriter::Write(output, original);
    Require(output.str().find("graphLastGoodArtifactAssetId 12001\n") != std::string::npos, "Material writer did not emit last-good artifact asset id");
    Require(output.str().find("graphLastGoodArtifactHash 3405648436\n") != std::string::npos, "Material writer did not emit last-good artifact hash");

    std::istringstream input{ output.str() };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Material graph last-good policy should parse");
    Require(result.Succeeded(), "Material graph last-good policy should not produce diagnostics");
    Require(result.asset->graph.lastGoodArtifact.IsValid(), "Material graph last-good artifact should be valid after parse");

    const RenderMaterialGraphArtifactDecision current = ResolveRenderMaterialGraphArtifactDecision(
        result.asset->graph,
        RenderMaterialGraphArtifactState{
            .compileState = RenderMaterialGraphArtifactCompileState::Ready,
            .currentArtifactAssetId = 777U,
        });
    Require(current.kind == RenderMaterialGraphArtifactDecisionKind::UseCurrentArtifact && current.artifactAssetId == 777U, "Ready material graph should use current artifact");
    const RenderMaterialGraphArtifactRuntimeDecision currentRuntime = ResolveRenderMaterialGraphArtifactRuntimeDecision(
        result.asset->graph,
        RenderMaterialGraphArtifactState{
            .compileState = RenderMaterialGraphArtifactCompileState::Ready,
            .currentArtifactAssetId = 777U,
        },
        RenderMaterialGraphBuildContext{ .assetId = 0x0205U, .sourcePath = "/Game/Materials/LastGood.kbmat" });
    Require(!currentRuntime.UsesFallback() && !currentRuntime.diagnostic.has_value(), "KBMAT-GRAPH-0205: Current graph artifact should not emit fallback diagnostics");

    const RenderMaterialGraphArtifactDecision failed = ResolveRenderMaterialGraphArtifactDecision(
        result.asset->graph,
        RenderMaterialGraphArtifactState{
            .compileState = RenderMaterialGraphArtifactCompileState::Failed,
            .currentArtifactAssetId = 0U,
        });
    Require(failed.kind == RenderMaterialGraphArtifactDecisionKind::UseLastGoodArtifact && failed.artifactAssetId == 12001U, "Failed material graph should use last-good artifact");
    const RenderMaterialGraphArtifactRuntimeDecision failedRuntime = ResolveRenderMaterialGraphArtifactRuntimeDecision(
        result.asset->graph,
        RenderMaterialGraphArtifactState{
            .compileState = RenderMaterialGraphArtifactCompileState::Failed,
            .currentArtifactAssetId = 0U,
        },
        RenderMaterialGraphBuildContext{ .assetId = 0x0205U, .sourcePath = "/Game/Materials/LastGood.kbmat" });
    Require(failedRuntime.UsesFallback() && failedRuntime.decision.kind == RenderMaterialGraphArtifactDecisionKind::UseLastGoodArtifact,
        "KBMAT-GRAPH-0205: Failed graph should use last-good runtime artifact when available");
    Require(failedRuntime.diagnostic.has_value() && failedRuntime.diagnostic->severity == RenderMaterialGraphDiagnosticSeverity::Warning &&
            failedRuntime.diagnostic->assetId == 0x0205U && failedRuntime.diagnostic->sourcePath == "/Game/Materials/LastGood.kbmat",
        "KBMAT-GRAPH-0205: Last-good runtime fallback should emit source-context warning diagnostics");

    RenderMaterialGraphDocument errorOnlyGraph = result.asset->graph;
    errorOnlyGraph.artifactFailurePolicy = RenderMaterialGraphArtifactFailurePolicy::ErrorMaterial;
    const RenderMaterialGraphArtifactDecision errorOnly = ResolveRenderMaterialGraphArtifactDecision(
        errorOnlyGraph,
        RenderMaterialGraphArtifactState{
            .compileState = RenderMaterialGraphArtifactCompileState::Pending,
            .currentArtifactAssetId = 0U,
        });
    Require(errorOnly.kind == RenderMaterialGraphArtifactDecisionKind::UseErrorMaterial && errorOnly.artifactAssetId == 0U, "ErrorMaterial policy should not use last-good artifact");
    const RenderMaterialGraphArtifactRuntimeDecision errorRuntime = ResolveRenderMaterialGraphArtifactRuntimeDecision(
        errorOnlyGraph,
        RenderMaterialGraphArtifactState{
            .compileState = RenderMaterialGraphArtifactCompileState::Pending,
            .currentArtifactAssetId = 0U,
        },
        RenderMaterialGraphBuildContext{ .assetId = 0x0205U, .sourcePath = "/Game/Materials/ErrorOnly.kbmat" });
    Require(errorRuntime.UsesFallback() && errorRuntime.decision.kind == RenderMaterialGraphArtifactDecisionKind::UseErrorMaterial,
        "KBMAT-GRAPH-0205: Graph without last-good artifact should choose explicit error material");
    Require(errorRuntime.diagnostic.has_value() && errorRuntime.diagnostic->severity == RenderMaterialGraphDiagnosticSeverity::Error &&
            errorRuntime.diagnostic->message.find("explicit error material") != std::string::npos,
        "KBMAT-GRAPH-0205: Error material fallback should emit error diagnostics instead of silent default");
}

void RunMaterialGraphMvpNodeKindsAndPinsTest() {
    Require(ParseRenderMaterialGraphNodeKind("Scalar") == RenderMaterialGraphNodeKind::ConstantScalar, "Material graph MVP should parse Scalar alias");
    Require(ParseRenderMaterialGraphNodeKind("ConstantBool") == RenderMaterialGraphNodeKind::ConstantBool, "Material graph MVP should parse ConstantBool node");
    Require(ParseRenderMaterialGraphNodeKind("Bool") == RenderMaterialGraphNodeKind::ConstantBool, "Material graph MVP should parse Bool alias");
    Require(ParseRenderMaterialGraphNodeKind("Constant2Vector") == RenderMaterialGraphNodeKind::ConstantVector2, "Material graph MVP should parse UE Constant2Vector alias");
    Require(ParseRenderMaterialGraphNodeKind("XY") == RenderMaterialGraphNodeKind::ConstantVector2, "Material graph MVP should parse XY alias");
    Require(ParseRenderMaterialGraphNodeKind("Vector") == RenderMaterialGraphNodeKind::ConstantVector, "Material graph MVP should parse Vector alias");
    Require(ParseRenderMaterialGraphNodeKind("Color") == RenderMaterialGraphNodeKind::ConstantColor, "Material graph MVP should parse Color alias");
    Require(ParseRenderMaterialGraphNodeKind("UV") == RenderMaterialGraphNodeKind::Uv, "Material graph MVP should parse UV token as the UV node");
    Require(ParseRenderMaterialGraphNodeKind("TextureCoordinate") == RenderMaterialGraphNodeKind::TextureCoordinate, "MAT-45 TextureCoordinate is a distinct tiling node and must round-trip to itself, not collapse to UV");
    const auto compileTextureCoordinate = [](std::string_view hint) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        graph.nodes.push_back(RenderMaterialGraphNode{
            .id = 2U,
            .kind = RenderMaterialGraphNodeKind::TextureCoordinate,
            .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = std::string{ hint } },
        });
        graph.nodes.push_back(RenderMaterialGraphNode{
            .id = 3U,
            .kind = RenderMaterialGraphNodeKind::TextureSample,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "texCoordProbe", .textureRole = "baseColor", .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb },
        });
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureCoordinate, 2U, "uv", RenderMaterialGraphNodeKind::TextureSample, 3U, "uv"));
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0810U });
    };
    const RenderMaterialGraphCompileResult defaultTextureCoordinate = compileTextureCoordinate({});
    const RenderMaterialGraphCompileResult legacyUv1TextureCoordinate = compileTextureCoordinate("1");
    const RenderMaterialGraphCompileResult tiledUv1TextureCoordinate = compileTextureCoordinate("2 3 1");
    Require(defaultTextureCoordinate.Succeeded() &&
            defaultTextureCoordinate.shader.source.find("ctx.uv0 * vec2(1.0, 1.0)") != std::string::npos,
        "MAT-81 TextureCoordinate default must sample UV0 with 1x tiling, not collapse to zero UVs");
    Require(legacyUv1TextureCoordinate.Succeeded() &&
            legacyUv1TextureCoordinate.shader.source.find("ctx.uv1 * vec2(1.0, 1.0)") != std::string::npos,
        "MAT-81 TextureCoordinate legacy hint '1' must select UV1 instead of being parsed as a tiling scalar");
    Require(tiledUv1TextureCoordinate.Succeeded() &&
            tiledUv1TextureCoordinate.shader.source.find("ctx.uv1 * vec2(2.0, 3.0)") != std::string::npos,
        "MAT-81 TextureCoordinate extended hint must preserve tiling and UV set");
    Require(ParseRenderMaterialGraphNodeKind("Abs") == RenderMaterialGraphNodeKind::Absolute, "Material graph utility math should parse Abs alias");
    Require(ParseRenderMaterialGraphNodeKind("Min") == RenderMaterialGraphNodeKind::Minimum, "Material graph utility math should parse Min alias");
    Require(ParseRenderMaterialGraphNodeKind("Max") == RenderMaterialGraphNodeKind::Maximum, "Material graph utility math should parse Max alias");
    Require(ParseRenderMaterialGraphNodeKind("Frac") == RenderMaterialGraphNodeKind::Fraction, "Material graph utility math should parse Frac alias");
    Require(ParseRenderMaterialGraphNodeKind("Sqrt") == RenderMaterialGraphNodeKind::SquareRoot, "Material graph utility math should parse Sqrt alias");
    Require(ParseRenderMaterialGraphNodeKind("Sin") == RenderMaterialGraphNodeKind::Sine, "Material graph utility math should parse Sin alias");
    Require(ParseRenderMaterialGraphNodeKind("Cos") == RenderMaterialGraphNodeKind::Cosine, "Material graph utility math should parse Cos alias");
    Require(ParseRenderMaterialGraphNodeKind("Dot") == RenderMaterialGraphNodeKind::DotProduct, "Material graph vector math should parse Dot alias");
    Require(ParseRenderMaterialGraphNodeKind("Cross") == RenderMaterialGraphNodeKind::CrossProduct, "Material graph vector math should parse Cross alias");
    Require(ParseRenderMaterialGraphNodeKind("NormalizeVector") == RenderMaterialGraphNodeKind::Normalize, "Material graph vector math should parse NormalizeVector alias");
    Require(ParseRenderMaterialGraphNodeKind("Length") == RenderMaterialGraphNodeKind::Length, "Material graph vector math should parse Length node");
    Require(ParseRenderMaterialGraphNodeKind("Distance") == RenderMaterialGraphNodeKind::Distance, "Material graph vector math should parse Distance node");
    Require(ParseRenderMaterialGraphNodeKind("ComponentMask") == RenderMaterialGraphNodeKind::StaticComponentMask, "MAT-50 ComponentMask must map to the swizzle-mask node (StaticComponentMask), not BreakVector");
    Require(ParseRenderMaterialGraphNodeKind("StaticComponentMaskParameter") == RenderMaterialGraphNodeKind::StaticComponentMask, "MAT-50 StaticComponentMaskParameter catalog alias must map to StaticComponentMask");
    Require(ParseRenderMaterialGraphNodeKind("ChannelMaskParameter") == RenderMaterialGraphNodeKind::StaticComponentMask, "MAT-50 ChannelMaskParameter catalog alias must map to StaticComponentMask");
    Require(ParseRenderMaterialGraphNodeKind("AppendVector") == RenderMaterialGraphNodeKind::AppendVector, "MAT-50 AppendVector is a distinct concatenation node and must round-trip to itself, not MakeVector");
    Require(ParseRenderMaterialGraphNodeKind("StaticSwitchParameter") == RenderMaterialGraphNodeKind::StaticSwitch, "MAT-39 StaticSwitchParameter catalog alias must map to StaticSwitch");
    Require(ParseRenderMaterialGraphNodeKind("TextureObjectParameter") == RenderMaterialGraphNodeKind::TextureObject, "MAT-31 TextureObjectParameter catalog alias must map to TextureObject");
    Require(ParseRenderMaterialGraphNodeKind("Step") == RenderMaterialGraphNodeKind::Step, "Material graph conditional math should parse Step node");
    Require(ParseRenderMaterialGraphNodeKind("SmoothStep") == RenderMaterialGraphNodeKind::SmoothStep, "Material graph conditional math should parse SmoothStep node");
    Require(ParseRenderMaterialGraphNodeKind("Compare") == RenderMaterialGraphNodeKind::If, "Material graph conditional math should parse Compare alias");
    Require(ParseRenderMaterialGraphNodeKind("RuntimeSwitch") == RenderMaterialGraphNodeKind::RuntimeSwitch, "Material graph conditional math should parse RuntimeSwitch alias");
    Require(IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::RuntimeSwitch, "index") &&
            IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::RuntimeSwitch, "case3") &&
            IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::RuntimeSwitch, "value") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::RuntimeSwitch, "index", false) == RenderMaterialGraphPinType::Float &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::RuntimeSwitch, "case0", false) == RenderMaterialGraphPinType::Float4,
        "Material graph runtime Switch should expose index/default/case pins and a Float4 output");
    Require(ParseRenderMaterialGraphNodeKind("Sobol") == RenderMaterialGraphNodeKind::Sobol &&
            ParseRenderMaterialGraphNodeKind("Sobol2D") == RenderMaterialGraphNodeKind::Sobol &&
            ParseRenderMaterialGraphNodeKind("LowDiscrepancy") == RenderMaterialGraphNodeKind::Sobol,
        "MAT-50 Sobol should parse its UE-style and low-discrepancy aliases");
    Require(RenderMaterialGraphNodeInputPinNames(RenderMaterialGraphNodeKind::Sobol) == std::vector<std::string>{ "cell", "index", "seed" } &&
            RenderMaterialGraphNodeOutputPinNames(RenderMaterialGraphNodeKind::Sobol) == std::vector<std::string>{ "value" },
        "MAT-50 Sobol should expose Cell/Index/Seed inputs and a value output");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::Sobol, "cell", false) == RenderMaterialGraphPinType::Float2 &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::Sobol, "index", false) == RenderMaterialGraphPinType::Float &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::Sobol, "seed", false) == RenderMaterialGraphPinType::Float2 &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::Sobol, "value", true) == RenderMaterialGraphPinType::Float2,
        "MAT-50 Sobol should keep its 2D sample schema visible to validation and codegen");
    Require(ParseRenderMaterialGraphNodeKind("Desaturation") == RenderMaterialGraphNodeKind::Desaturate, "Material graph surface utility should parse Desaturation alias");
    Require(ParseRenderMaterialGraphNodeKind("Fresnel") == RenderMaterialGraphNodeKind::Fresnel, "Material graph surface utility should parse Fresnel node");
    Require(ParseRenderMaterialGraphNodeKind("ObjectBounds") == RenderMaterialGraphNodeKind::ObjectBounds, "MAT-46 ObjectBounds must parse as a world/object-space node");
    Require(ParseRenderMaterialGraphNodeKind("ObjectOrientation") == RenderMaterialGraphNodeKind::ObjectOrientation, "MAT-46 ObjectOrientation must parse as a world/object-space node");
    Require(ParseRenderMaterialGraphNodeKind("TwoSidedSign") == RenderMaterialGraphNodeKind::TwoSidedSign, "MAT-46 TwoSidedSign must parse as a world/object-space node");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::TwoSidedSign, "value", true) == RenderMaterialGraphPinType::Float,
        "MAT-46 TwoSidedSign.value must be a scalar float output");
    Require(ParseRenderMaterialGraphNodeKind("PerInstanceFadeAmount") == RenderMaterialGraphNodeKind::PerInstanceFadeAmount, "MAT-47 PerInstanceFadeAmount must parse as a vertex/instance-data node");
    Require(ParseRenderMaterialGraphNodeKind("PerInstanceCustomData0") == RenderMaterialGraphNodeKind::PerInstanceCustomData, "MAT-47 PerInstanceCustomData alias must parse as the custom-data node");
    Require(ParseRenderMaterialGraphNodeKind("PreSkinnedLocalPosition") == RenderMaterialGraphNodeKind::PreSkinnedPosition, "MAT-47 PreSkinnedLocalPosition alias must parse as PreSkinnedPosition");
    Require(ParseRenderMaterialGraphNodeKind("PreSkinnedLocalNormal") == RenderMaterialGraphNodeKind::PreSkinnedNormal, "MAT-47 PreSkinnedLocalNormal alias must parse as PreSkinnedNormal");
    Require(ParseRenderMaterialGraphNodeKind("DeltaTime") == RenderMaterialGraphNodeKind::DeltaTime, "MAT-30 DeltaTime must parse as a time/animation node");
    Require(ParseRenderMaterialGraphNodeKind("TimeDelta") == RenderMaterialGraphNodeKind::DeltaTime, "MAT-30 TimeDelta alias must parse as DeltaTime");
    Require(ParseRenderMaterialGraphNodeKind("DynamicParameters") == RenderMaterialGraphNodeKind::DynamicParameter, "MAT-30 DynamicParameters alias must parse as DynamicParameter");
    Require(ParseRenderMaterialGraphNodeKind("Minus") == RenderMaterialGraphNodeKind::Negate, "Material graph advanced math should parse Minus alias");
    Require(ParseRenderMaterialGraphNodeKind("Sign") == RenderMaterialGraphNodeKind::Sign, "Material graph advanced math should parse Sign node");
    Require(ParseRenderMaterialGraphNodeKind("Round") == RenderMaterialGraphNodeKind::Round, "Material graph advanced math should parse Round node");
    Require(ParseRenderMaterialGraphNodeKind("Trunc") == RenderMaterialGraphNodeKind::Truncate, "Material graph advanced math should parse Trunc alias");
    Require(ParseRenderMaterialGraphNodeKind("Tan") == RenderMaterialGraphNodeKind::Tangent, "Material graph advanced math should parse Tan alias");
    Require(ParseRenderMaterialGraphNodeKind("Asin") == RenderMaterialGraphNodeKind::ArcSine, "Material graph advanced math should parse Asin alias");
    Require(ParseRenderMaterialGraphNodeKind("Acos") == RenderMaterialGraphNodeKind::ArcCosine, "Material graph advanced math should parse Acos alias");
    Require(ParseRenderMaterialGraphNodeKind("Atan") == RenderMaterialGraphNodeKind::ArcTangent, "Material graph advanced math should parse Atan alias");
    Require(ParseRenderMaterialGraphNodeKind("Atan2") == RenderMaterialGraphNodeKind::ArcTangent2, "Material graph advanced math should parse Atan2 alias");
    Require(ParseRenderMaterialGraphNodeKind("AsinFast") == RenderMaterialGraphNodeKind::ArcSineFast, "Material graph fast trig should parse AsinFast alias");
    Require(ParseRenderMaterialGraphNodeKind("AcosFast") == RenderMaterialGraphNodeKind::ArcCosineFast, "Material graph fast trig should parse AcosFast alias");
    Require(ParseRenderMaterialGraphNodeKind("AtanFast") == RenderMaterialGraphNodeKind::ArcTangentFast, "Material graph fast trig should parse AtanFast alias");
    Require(ParseRenderMaterialGraphNodeKind("Atan2Fast") == RenderMaterialGraphNodeKind::ArcTangent2Fast, "Material graph fast trig should parse Atan2Fast alias");
    Require(RenderMaterialGraphNodeKindName(RenderMaterialGraphNodeKind::ArcSineFast) == "ArcSineFast", "Material graph fast trig names should round-trip");
    Require(RenderMaterialGraphNodeSupportStatus(RenderMaterialGraphNodeKind::ArcTangent2Fast) == RenderMaterialGraphNodeSupport::Production,
        "Material graph fast trig nodes should be production-supported once they have shader codegen");
    Require(IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::ArcSineFast, "value") &&
            IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::ArcSineFast, "value") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ArcSineFast, "value", true) == RenderMaterialGraphPinType::Float4,
        "Material graph fast unary trig should expose a Float4 value input/output schema");
    Require(IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::ArcTangent2Fast, "y") &&
            IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::ArcTangent2Fast, "x") &&
            IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::ArcTangent2Fast, "value") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ArcTangent2Fast, "value", true) == RenderMaterialGraphPinType::Float4,
        "Material graph fast atan2 should expose y/x inputs and a Float4 value output");

    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 1 1 1 1\n"
        "graphVersion 1\n"
        "graphNode 1 MaterialOutput 640 240\n"
        "graphNode 2 TextureSample 120 80\n"
        "graphNode 3 NormalUnpack 320 120\n"
        "graphNode 4 Scalar 100 220\n"
        "graphNode 5 Vector 100 320\n"
        "graphNode 6 Color 100 420\n"
        "graphNode 7 Add 300 220\n"
        "graphNode 8 Multiply 460 220\n"
        "graphNode 9 Clamp 620 220\n"
        "graphNode 10 Lerp 780 220\n"
        "graphNode 11 UV -80 80\n"
        "graphLink 11 uv 2 uv\n"
        "graphLink 2 color 3 color\n"
        "graphLink 3 normal 1 normal\n"
        "graphLink 4 value 7 a\n"
        "graphLink 4 value 7 b\n"
        "graphLink 7 value 8 a\n"
        "graphLink 4 value 8 b\n"
        "graphLink 8 value 9 value\n"
        "graphLink 4 value 9 min\n"
        "graphLink 4 value 9 max\n"
        "graphLink 9 value 10 a\n"
        "graphLink 6 rgba 10 b\n"
        "graphLink 4 value 10 t\n"
        "graphLink 10 value 1 baseColor\n"
        "graphLink 5 xyz 1 emissive\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Material graph MVP node fixture should parse");
    Require(result.Succeeded(), "Material graph MVP node fixture should not produce diagnostics");
    Require(result.asset->graph.nodes.size() == 11U, "Material graph MVP fixture lost nodes");
    Require(result.asset->graph.links.size() == 15U, "Material graph MVP fixture lost links");
    Require(result.asset->graph.nodes[4].kind == RenderMaterialGraphNodeKind::ConstantVector, "Material graph MVP vector alias was not normalized");
    Require(result.asset->graph.nodes[6].kind == RenderMaterialGraphNodeKind::Add, "Material graph MVP Add node was not parsed");
    Require(result.asset->graph.nodes[7].kind == RenderMaterialGraphNodeKind::Multiply, "Material graph MVP Multiply node was not parsed");
    Require(result.asset->graph.nodes[8].kind == RenderMaterialGraphNodeKind::Clamp, "Material graph MVP Clamp node was not parsed");
    Require(result.asset->graph.nodes[9].kind == RenderMaterialGraphNodeKind::Lerp, "Material graph MVP Lerp node was not parsed");
    Require(result.asset->graph.nodes[10].kind == RenderMaterialGraphNodeKind::Uv, "Material graph MVP UV node was not parsed");
    Require(IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::DeltaTime, "value"), "MAT-30 DeltaTime must expose a value output pin");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::DeltaTime, "value", true) == RenderMaterialGraphPinType::Float, "MAT-30 DeltaTime value pin must be float");
    Require(IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::DynamicParameter, "rgba"), "MAT-30 DynamicParameter must expose an rgba output pin");
    Require(IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::DynamicParameter, "r"), "MAT-30 DynamicParameter must expose channel output pins");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::DynamicParameter, "rgba", true) == RenderMaterialGraphPinType::Color, "MAT-30 DynamicParameter rgba pin must be color");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::DynamicParameter, "b", true) == RenderMaterialGraphPinType::Float, "MAT-30 DynamicParameter channel pins must be float");

    std::ostringstream output;
    RenderMaterialAssetWriter::Write(output, *result.asset);
    Require(output.str().find("graphNode 5 ConstantVector 100 320\n") != std::string::npos, "Material graph writer should canonicalize Vector alias");
    Require(output.str().find("graphNode 10 Lerp 780 220\n") != std::string::npos, "Material graph writer should emit Lerp node");
    Require(output.str().find("graphNode 11 UV -80 80\n") != std::string::npos, "Material graph writer should emit UV node");
    const RenderMaterialGraphLink& normalLink = result.asset->graph.links[2];
    const std::string expectedNormalLink = "graphLink " + std::to_string(normalLink.id) + " 3 " + std::to_string(normalLink.fromPinId) + " normal 1 " + std::to_string(normalLink.toPinId) + " normal\n";
    Require(output.str().find(expectedNormalLink) != std::string::npos, "Material graph writer should preserve NormalUnpack output link with stable ids");
}

void RunMaterialGraphRejectsPartialLastGoodArtifactTest() {
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 1 1 1 1\n"
        "graphVersion 1\n"
        "graphArtifactFailurePolicy LastGoodThenErrorMaterial\n"
        "graphLastGoodArtifactAssetId 12001\n"
        "graphNode 1 MaterialOutput 640 240\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(!result.asset.has_value(), "Partial last-good artifact should fail parsing");
    Require(!result.diagnostics.empty(), "Partial last-good artifact should produce a diagnostic");
    Require(result.diagnostics.back().code == RenderMaterialAssetParseDiagnosticCode::InvalidGraphField, "Partial last-good artifact should use graph diagnostic");
    Require(result.diagnostics.back().field == "graphLastGoodArtifact", "Partial last-good artifact diagnostic should identify the artifact");
    Require(result.diagnostics.back().line == 7U, "KBMAT-1004: Partial last-good artifact diagnostic should keep the source line");
}

void RunMaterialGraphRejectsInvalidLinksTest() {
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 1 1 1 1\n"
        "graphVersion 1\n"
        "graphNode 1 MaterialOutput 640 240\n"
        "graphNode 2 ConstantScalar 240 180\n"
        "graphLink 2 rgba 1 baseColor\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(!result.asset.has_value(), "Invalid graph link should fail parsing");
    Require(!result.diagnostics.empty(), "Invalid graph link should produce a diagnostic");
    Require(result.diagnostics.back().code == RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, "Invalid graph link should use typed diagnostic code");
    Require(result.diagnostics.back().field == "graphLink", "Invalid graph link diagnostic should identify graphLink");
}

void RunMaterialGraphTypedPinCompatibilityTest() {
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ConstantScalar, "value", true) == RenderMaterialGraphPinType::Float, "KBMAT-GRAPH-0103: Scalar output should be float");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ConstantBool, "value", true) == RenderMaterialGraphPinType::Bool, "KBMAT-GRAPH-0103: ConstantBool output should be bool");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::Uv, "uv", true) == RenderMaterialGraphPinType::Float2, "KBMAT-GRAPH-0103: UV output should be float2");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ConstantVector2, "xy", true) == RenderMaterialGraphPinType::Float2, "KBMAT-GRAPH-0103: Constant2Vector output should be float2");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ConstantVector, "xyz", true) == RenderMaterialGraphPinType::Float3, "KBMAT-GRAPH-0103: Vector output should be float3");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ConstantColor, "rgba", true) == RenderMaterialGraphPinType::Color, "KBMAT-GRAPH-0103: Color output should be color");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ParameterTexture, "texture", true) == RenderMaterialGraphPinType::Texture2D, "KBMAT-GRAPH-0103: Texture parameter output should be texture2D");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::NormalUnpack, "normal", true) == RenderMaterialGraphPinType::Normal, "KBMAT-GRAPH-0103: NormalUnpack output should be normal");
    Require(RenderMaterialGraphPinTypeName(RenderMaterialGraphPinType::Sampler) == "sampler", "KBMAT-GRAPH-0103: Pin type enum should expose sampler");
    Require(RenderMaterialGraphPinTypeName(RenderMaterialGraphPinType::Bool) == "bool", "KBMAT-GRAPH-0103: Pin type enum should expose bool");
    Require(AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType::Bool, RenderMaterialGraphPinType::Float), "KBMAT-GRAPH-0103: Bool should feed float inputs through explicit coercion");
    Require(AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType::Bool, RenderMaterialGraphPinType::Color), "KBMAT-GRAPH-0103: Bool should feed color inputs through explicit coercion");
    Require(AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType::Float, RenderMaterialGraphPinType::Bool), "KBMAT-GRAPH-0103: Float should feed bool inputs through explicit coercion");
    Require(AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType::Color, RenderMaterialGraphPinType::Float4), "KBMAT-GRAPH-0103: Color should feed float4 operator inputs");
    Require(AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType::Float2, RenderMaterialGraphPinType::Color) &&
            AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType::Color, RenderMaterialGraphPinType::Float2),
        "KBMAT-GRAPH-0103: Float2 nodes such as Sobol should feed color pins and receive color defaults through explicit coercion");
    Require(AreRenderMaterialGraphPinsCompatible(
                RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ConstantVector2, "xy", true),
                RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::TextureSample, "uv", false)),
        "KBMAT-GRAPH-0103: Constant2Vector should feed TextureSample UV input");
    Require(AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType::Normal, RenderMaterialGraphPinType::Float3), "KBMAT-GRAPH-0103: Normal should feed float3-compatible inputs");
    Require(!AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType::Texture2D, RenderMaterialGraphPinType::Color), "KBMAT-GRAPH-0103: Texture2D should not feed color inputs directly");

    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 1 1 1 1\n"
        "graphVersion 1\n"
        "graphNode 1 MaterialOutput 640 240\n"
        "graphNode 2 ParameterTexture 120 80\n"
        "graphLink 2 texture 1 baseColor\n"
    };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(!result.asset.has_value(), "KBMAT-GRAPH-0103: Typed pin mismatch should fail material graph parsing");
    Require(!result.diagnostics.empty(), "KBMAT-GRAPH-0103: Typed pin mismatch should produce diagnostics");
    Require(result.diagnostics.back().code == RenderMaterialAssetParseDiagnosticCode::InvalidGraphLink, "KBMAT-GRAPH-0103: Typed pin mismatch should use graph link diagnostic");
    Require(result.diagnostics.back().message.find("texture2D") != std::string::npos &&
            result.diagnostics.back().message.find("color") != std::string::npos,
        "KBMAT-GRAPH-0103: Typed pin mismatch diagnostic should name source and target types");

    RenderMaterialGraphDocument boolGraph = MakeDefaultRenderMaterialGraphDocument();
    boolGraph.shadingModel = "unlit";
    boolGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantBool,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "true" },
    });
    boolGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantBool, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult boolCompile = CompileRenderMaterialGraphToShaderSource(boolGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0103B00U });
    Require(boolCompile.Succeeded(), "KBMAT-GRAPH-0103: ConstantBool -> BaseColor graph should compile through bool-to-color coercion");
    Require(boolCompile.shader.source.find("true") != std::string::npos && boolCompile.shader.source.find("? 1.0 : 0.0") != std::string::npos,
        "KBMAT-GRAPH-0103: ConstantBool codegen should emit an explicit bool-to-numeric coercion");
}

void RunMaterialGraphParameterNodesGenerateMaterialTypeSchemaTest() {
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 1 1 1 1\n"
        "graphVersion 1\n"
        "graphNode 1 MaterialOutput 640 240\n"
        "graphNode 2 ParameterScalar 100 100\n"
        "graphParameter 2 roughnessScale Roughness%20Scale Surface 0.5 0 1 _ Unknown true 10 Artist%20roughness%20control\n"
        "graphNode 3 ParameterColor 100 220\n"
        "graphParameter 3 tintColor Tint%20Color Core 1%201%201%201 0 1 _ Srgb false 20 Tint%20parameter\n"
        "graphNode 4 ParameterTexture 100 340\n"
        "graphParameter 4 detailAlbedo Detail%20Albedo Core White _ _ baseColor Srgb true 30 Detail%20base%20color\n"
    };

    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    const std::string parseMessage = "KBMAT-GRAPH-0105: Material graph parameter fixture should parse: " + result.ErrorMessage();
    Require(result.asset.has_value(), parseMessage.c_str());
    Require(result.Succeeded(), "KBMAT-GRAPH-0105: Material graph parameter fixture should not produce diagnostics");
    const RenderMaterialGraphNode* scalarNode = FindRenderMaterialGraphNode(result.asset->graph, 2U);
    Require(scalarNode != nullptr && scalarNode->parameter.stableId == "roughnessScale" &&
            scalarNode->parameter.displayName == "Roughness Scale" &&
            scalarNode->parameter.group == RenderMaterialParameterGroup::Surface &&
            scalarNode->parameter.defaultValueHint == "0.5" &&
            scalarNode->parameter.hasRange && NearlyEqual(scalarNode->parameter.rangeMin, 0.0F) &&
            NearlyEqual(scalarNode->parameter.rangeMax, 1.0F) &&
            scalarNode->parameter.overrideSupported,
        "KBMAT-GRAPH-0105: Graph parameter metadata did not round-trip into scalar node");

    const RenderMaterialTypeSchema schema = BuildRenderMaterialGraphParameterSchema(result.asset->graph, "graph.surface", 3U);
    Require(schema.typeName == "graph.surface" && schema.typeVersion == 3U, "KBMAT-GRAPH-0105: Generated schema lost graph material type identity");
    Require(schema.parameters.size() == 2U, "KBMAT-GRAPH-0105: Generated schema should expose scalar and color parameters");
    Require(schema.textureSlots.size() == 1U, "KBMAT-GRAPH-0105: Generated schema should expose texture parameter as texture slot");

    const RenderMaterialParameterSchema* roughness = FindMaterialParameterSchema(schema, "roughnessScale");
    Require(roughness != nullptr &&
            roughness->displayName == "Roughness Scale" &&
            roughness->type == RenderMaterialParameterType::Scalar &&
            roughness->group == RenderMaterialParameterGroup::Surface &&
            roughness->defaultValueHint == "0.5" &&
            roughness->range.has_value() &&
            NearlyEqual(roughness->range->min, 0.0F) &&
            NearlyEqual(roughness->range->max, 1.0F) &&
            roughness->overrideSupported &&
            roughness->editorOrder == 10U,
        "KBMAT-GRAPH-0105: Scalar parameter node did not generate complete Material Type parameter schema");

    const RenderMaterialParameterSchema* tint = FindMaterialParameterSchema(schema, "tintColor");
    Require(tint != nullptr &&
            tint->type == RenderMaterialParameterType::Color &&
            tint->group == RenderMaterialParameterGroup::Core &&
            tint->defaultValueHint == "1 1 1 1" &&
            tint->range.has_value() &&
            !tint->overrideSupported,
        "KBMAT-GRAPH-0105: Color parameter node did not generate default/range/override schema");

    const RenderMaterialTextureSlotSchema* detail = FindMaterialTextureSlotSchema(schema, "detailAlbedoTextureAssetId");
    Require(detail != nullptr &&
            detail->name == "Detail Albedo" &&
            detail->role == "baseColor" &&
            detail->pathFieldName == "detailAlbedoTexture" &&
            detail->expectedColorSpace == RenderMaterialTextureColorSpace::Srgb &&
            detail->fallbackDescription == "White" &&
            detail->overrideSupported &&
            detail->editorOrder == 30U,
        "KBMAT-GRAPH-0105: Texture parameter node did not generate role/color-space/override texture slot schema");

    std::ostringstream output;
    RenderMaterialAssetWriter::Write(output, *result.asset);
    Require(output.str().find("graphParameter 2 roughnessScale Roughness%20Scale Surface 0.5 0.000000 1.000000 _ Unknown true 10 Artist%20roughness%20control\n") != std::string::npos,
        "KBMAT-GRAPH-0105: Material graph writer should persist scalar parameter metadata");
    Require(output.str().find("graphParameter 4 detailAlbedo Detail%20Albedo Core White _ _ baseColor Srgb true 30 Detail%20base%20color\n") != std::string::npos,
        "KBMAT-GRAPH-0105: Material graph writer should persist texture role and color-space metadata");
}

void RunMaterialGraphRejectsInvalidParameterMetadataTest() {
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 1 1 1 1\n"
        "graphVersion 1\n"
        "graphNode 1 MaterialOutput 640 240\n"
        "graphNode 2 ParameterScalar 100 100\n"
        "graphParameter 2 badScalar Bad%20Scalar Surface 0.5 1 0 _ Unknown true 10 Bad%20range\n"
    };

    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(!result.asset.has_value(), "KBMAT-GRAPH-0105: Invalid parameter range should fail material graph parsing");
    Require(!result.diagnostics.empty(), "KBMAT-GRAPH-0105: Invalid parameter range should produce diagnostics");
    Require(result.diagnostics.back().code == RenderMaterialAssetParseDiagnosticCode::InvalidGraphNode,
        "KBMAT-GRAPH-0105: Invalid parameter metadata should use graph node diagnostics");
    Require(result.diagnostics.back().field == "graphParameter",
        "KBMAT-GRAPH-0105: Invalid parameter metadata diagnostic should identify graphParameter");
}

void RunMaterialGraphRuntimeDiagnosticsTest() {
    RenderMaterialGraphDocument graph{};
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 1U,
        .kind = RenderMaterialGraphNodeKind::MaterialOutput,
        .positionX = 640,
        .positionY = 240,
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = static_cast<RenderMaterialGraphNodeKind>(255U),
        .positionX = 0,
        .positionY = 0,
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 120,
        .positionY = 80,
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::ParameterTexture,
        .positionX = 120,
        .positionY = 180,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "badNormal",
            .displayName = "Bad Normal",
            .textureRole = "normal",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
        },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 5U,
        .kind = RenderMaterialGraphNodeKind::Add,
        .positionX = 300,
        .positionY = 160,
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 6U,
        .kind = RenderMaterialGraphNodeKind::Multiply,
        .positionX = 460,
        .positionY = 160,
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 7U,
        .kind = RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = 100,
        .positionY = 260,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "sharedParam",
            .displayName = "Shared Param A",
        },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 8U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = 100,
        .positionY = 340,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "sharedParam",
            .displayName = "Shared Param B",
        },
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterTexture, 4U, "texture", RenderMaterialGraphNodeKind::Add, 5U, "b"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Add, 5U, "value", RenderMaterialGraphNodeKind::Multiply, 6U, "a"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Multiply, 6U, "value", RenderMaterialGraphNodeKind::Add, 5U, "a"));

    const std::vector<RenderMaterialGraphDiagnostic> diagnostics = ValidateRenderMaterialGraphDocument(graph);
    Require(!HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::DisconnectedRequiredOutput),
        "KBMAT-GRAPH-0106: Disconnected Material Output BaseColor should use MaterialSurface default fallback without a graph error");
    Require(HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::TypeMismatch),
        "KBMAT-GRAPH-0106: Graph diagnostics should report type mismatches");
    Require(HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::Cycle),
        "KBMAT-GRAPH-0106: Graph diagnostics should report cycles");
    Require(HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedNode),
        "KBMAT-GRAPH-0106: Graph diagnostics should report unsupported nodes");
    Require(!HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::MissingTexture),
        "KBMAT-GRAPH-0106: TextureSample nodes without texture input should use inline texture slot defaults");
    Require(HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::InvalidColorSpaceRole),
        "KBMAT-GRAPH-0106: Graph diagnostics should report invalid texture role/color-space pairs");
    Require(HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::DuplicateParameterStableId),
        "KBMAT-GRAPH-0106: Graph diagnostics should reject duplicate parameter stable ids");

    RenderMaterialAssetData asset{};
    asset.graph = graph;
    asset.desc.alphaMode = RenderMaterialAlphaMode::Blend;
    const std::vector<RenderMaterialGraphDiagnostic> assetDiagnostics = ValidateRenderMaterialAssetGraphDiagnostics(asset);
    Require(HasGraphDiagnostic(assetDiagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedBlendMode),
        "KBMAT-GRAPH-0106: Material graph diagnostics should report unsupported blend mode");
}

void RunMaterialGraphTypedIrBuildTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 80,
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphIrBuildResult result = BuildRenderMaterialGraphIr(
        graph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0201U,
            .sourcePath = "/Game/Materials/TypedIr.kbmat",
        });
    Require(result.Succeeded(), "KBMAT-GRAPH-0201: Valid material graph should build typed IR without errors");
    Require(result.ir.nodes.size() == 2U, "KBMAT-GRAPH-0201: Typed graph IR should contain all known nodes");
    Require(result.ir.links.size() == 1U, "KBMAT-GRAPH-0201: Typed graph IR should contain compatible links");
    Require(result.ir.outputBindings.size() == 1U, "KBMAT-GRAPH-0201: Typed graph IR should expose Material Output bindings");
    Require(result.ir.outputBindings[0].outputPin == "baseColor", "KBMAT-GRAPH-0201: Typed graph IR output binding should identify the output pin");
    Require(result.ir.outputBindings[0].outputPinId == RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
        "KBMAT-GRAPH-0201: Typed graph IR output binding should carry stable output pin id");
    Require(result.ir.outputBindings[0].sourceType == RenderMaterialGraphPinType::Color && result.ir.outputBindings[0].outputType == RenderMaterialGraphPinType::Color,
        "KBMAT-GRAPH-0201: Typed graph IR output binding should carry resolved pin types");

    RenderMaterialGraphDocument invalid = MakeDefaultRenderMaterialGraphDocument();
    invalid.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterTexture,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "normalMap",
            .displayName = "Normal Map",
            .textureRole = "normal",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Linear,
        },
    });
    invalid.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::Add,
        .positionX = 320,
        .positionY = 80,
    });
    invalid.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterTexture, 2U, "texture", RenderMaterialGraphNodeKind::Add, 3U, "b"));

    const RenderMaterialGraphIrBuildResult invalidResult = BuildRenderMaterialGraphIr(
        invalid,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0201U,
            .sourcePath = "/Game/Materials/BrokenTypedIr.kbmat",
        });
    Require(!invalidResult.Succeeded(), "KBMAT-GRAPH-0201: Invalid material graph IR build should fail on error diagnostics");
    const RenderMaterialGraphDiagnostic* typeMismatch = FindGraphDiagnostic(invalidResult.diagnostics, RenderMaterialGraphDiagnosticKind::TypeMismatch);
    Require(typeMismatch != nullptr, "KBMAT-GRAPH-0201: Typed graph IR diagnostics should include type mismatch");
    Require(typeMismatch->assetId == 0x0201U && typeMismatch->sourcePath == "/Game/Materials/BrokenTypedIr.kbmat",
        "KBMAT-GRAPH-0201: Typed graph IR diagnostics should carry asset id and source path");
    Require(typeMismatch->nodeId == 3U && typeMismatch->pin == "b",
        "KBMAT-GRAPH-0201: Typed graph IR diagnostics should carry node id and pin name");
    Require(typeMismatch->pinId == RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::Add, "b", false),
        "KBMAT-GRAPH-0201: Typed graph IR diagnostics should carry stable pin id");
}

void RunMaterialGraphShaderSourceCompilerMvpTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 80,
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult result = CompileRenderMaterialGraphToShaderSource(
        graph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/CompiledColor.kbmat",
        });
    Require(result.Succeeded(), "KBMAT-GRAPH-0202: Constant color graph should compile to shader source");
    Require(result.shader.entryPoint == "EvaluateMaterialGraph", "KBMAT-GRAPH-0202: Shader compiler should expose the generated entry point");
    Require(result.shader.sourceHash != 0U, "KBMAT-GRAPH-0202: Shader compiler should hash generated source");
    Require(result.shader.source.find("struct MaterialSurface") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should generate a MaterialSurface struct");
    Require(result.shader.source.find("struct MaterialGraphContext") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should generate a MaterialGraphContext struct");
    Require(result.shader.source.find("MaterialSurface EvaluateMaterialGraph(MaterialGraphContext ctx)") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should generate a material graph entry function with context parameter");
    Require(result.shader.source.find("float alphaClipThreshold;") != std::string::npos,
        "KBMAT-GRAPH-0202: MaterialSurface struct should include alphaClipThreshold field");
    Require(result.shader.source.find("material.baseColor = vec4(1.0, 1.0, 1.0, 1.0);") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit connected BaseColor expression");
    Require(result.shader.source.find("material.roughness = 1.0;") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit PBR default roughness");

    RenderMaterialGraphDocument authoredConstantGraph = MakeDefaultRenderMaterialGraphDocument();
    authoredConstantGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{
            .defaultValueHint = "0.25 0.5 0.75 1",
        },
    });
    authoredConstantGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 200,
        .parameter = RenderMaterialGraphParameterMetadata{
            .defaultValueHint = "0.42",
        },
    });
    authoredConstantGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    authoredConstantGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    const RenderMaterialGraphCompileResult authoredConstantResult = CompileRenderMaterialGraphToShaderSource(
        authoredConstantGraph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/AuthoredConstants.kbmat",
        });
    Require(authoredConstantResult.Succeeded(), "KBMAT-GRAPH-0202: Authored constant graph should compile");
    Require(authoredConstantResult.shader.source.find("material.baseColor = vec4(0.25, 0.5, 0.75, 1.0);") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit authored ConstantColor values");
    Require(authoredConstantResult.shader.source.find("material.roughness = 0.42;") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit authored ConstantScalar values");

    RenderMaterialGraphDocument authoredUvGraph = MakeDefaultRenderMaterialGraphDocument();
    authoredUvGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector2,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{
            .defaultValueHint = "0.25 0.75",
        },
    });
    authoredUvGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 320,
        .positionY = 80,
    });
    authoredUvGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector2, 2U, "xy", RenderMaterialGraphNodeKind::TextureSample, 3U, "uv"));
    authoredUvGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult authoredUvResult = CompileRenderMaterialGraphToShaderSource(
        authoredUvGraph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/AuthoredUvConstant.kbmat",
        });
    Require(authoredUvResult.Succeeded(), "KBMAT-GRAPH-0202: Constant2Vector UV graph should compile");
    Require(authoredUvResult.shader.source.find("vec2(0.25, 0.75)") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit authored Constant2Vector values");

    RenderMaterialGraphDocument mathGraph = MakeDefaultRenderMaterialGraphDocument();
    mathGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.8" },
    });
    mathGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 160,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.3" },
    });
    mathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::Subtract, .positionX = 300, .positionY = 120 });
    mathGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 5U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 300,
        .positionY = 240,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "2" },
    });
    mathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 6U, .kind = RenderMaterialGraphNodeKind::Power, .positionX = 460, .positionY = 120 });
    mathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 7U, .kind = RenderMaterialGraphNodeKind::OneMinus, .positionX = 620, .positionY = 120 });
    mathGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 8U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 460,
        .positionY = 300,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.15" },
    });
    mathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 9U, .kind = RenderMaterialGraphNodeKind::Divide, .positionX = 620, .positionY = 300 });
    mathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::Subtract, 4U, "a"));
    mathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::Subtract, 4U, "b"));
    mathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Subtract, 4U, "value", RenderMaterialGraphNodeKind::Power, 6U, "base"));
    mathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 5U, "value", RenderMaterialGraphNodeKind::Power, 6U, "exponent"));
    mathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Power, 6U, "value", RenderMaterialGraphNodeKind::OneMinus, 7U, "value"));
    mathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::OneMinus, 7U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    mathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::Divide, 9U, "a"));
    mathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::Divide, 9U, "b"));
    mathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Divide, 9U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
    const RenderMaterialGraphCompileResult mathResult = CompileRenderMaterialGraphToShaderSource(
        mathGraph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/CompiledMath.kbmat",
        });
    Require(mathResult.Succeeded(), "KBMAT-GRAPH-0202: Math graph should compile to shader source");
    Require(mathResult.shader.source.find(" - ") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Subtract expression");
    Require(mathResult.shader.source.find("/ max(abs(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit guarded Divide expression");
    Require(mathResult.shader.source.find("pow(max(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Power expression");
    Require(mathResult.shader.source.find("vec4_splat(1.0) -") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit OneMinus expression");

    RenderMaterialGraphDocument utilityGraph = MakeDefaultRenderMaterialGraphDocument();
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "-0.25" },
    });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::Absolute, .positionX = 280, .positionY = 80 });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 160,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.4" },
    });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 5U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 240,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.8" },
    });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 6U, .kind = RenderMaterialGraphNodeKind::Minimum, .positionX = 280, .positionY = 200 });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 7U, .kind = RenderMaterialGraphNodeKind::Maximum, .positionX = 440, .positionY = 140 });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 8U, .kind = RenderMaterialGraphNodeKind::Saturate, .positionX = 600, .positionY = 140 });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 9U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 320,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1.75" },
    });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 10U, .kind = RenderMaterialGraphNodeKind::Fraction, .positionX = 280, .positionY = 320 });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 11U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 400,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.25" },
    });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 12U, .kind = RenderMaterialGraphNodeKind::SquareRoot, .positionX = 280, .positionY = 400 });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 13U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 480,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.9" },
    });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 14U, .kind = RenderMaterialGraphNodeKind::Floor, .positionX = 280, .positionY = 480 });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 15U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 560,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.1" },
    });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 16U, .kind = RenderMaterialGraphNodeKind::Ceil, .positionX = 280, .positionY = 560 });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 17U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 640,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0" },
    });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 18U, .kind = RenderMaterialGraphNodeKind::Sine, .positionX = 280, .positionY = 640 });
    utilityGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 19U, .kind = RenderMaterialGraphNodeKind::Cosine, .positionX = 440, .positionY = 640 });
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::Absolute, 3U, "value"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Minimum, 6U, "a"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 5U, "value", RenderMaterialGraphNodeKind::Minimum, 6U, "b"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Absolute, 3U, "value", RenderMaterialGraphNodeKind::Maximum, 7U, "a"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Minimum, 6U, "value", RenderMaterialGraphNodeKind::Maximum, 7U, "b"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Maximum, 7U, "value", RenderMaterialGraphNodeKind::Saturate, 8U, "value"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Saturate, 8U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 9U, "value", RenderMaterialGraphNodeKind::Fraction, 10U, "value"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Fraction, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 11U, "value", RenderMaterialGraphNodeKind::SquareRoot, 12U, "value"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::SquareRoot, 12U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::Floor, 14U, "value"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Floor, 14U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 15U, "value", RenderMaterialGraphNodeKind::Ceil, 16U, "value"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Ceil, 16U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 17U, "value", RenderMaterialGraphNodeKind::Sine, 18U, "value"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Sine, 18U, "value", RenderMaterialGraphNodeKind::Cosine, 19U, "value"));
    utilityGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Cosine, 19U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));
    const RenderMaterialGraphCompileResult utilityResult = CompileRenderMaterialGraphToShaderSource(
        utilityGraph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/CompiledUtilityMath.kbmat",
        });
    Require(utilityResult.Succeeded(), "KBMAT-GRAPH-0202: Utility math graph should compile to shader source");
    Require(utilityResult.shader.source.find("abs(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Abs expression");
    Require(utilityResult.shader.source.find("min(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Min expression");
    Require(utilityResult.shader.source.find("max(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Max expression");
    Require(utilityResult.shader.source.find("clamp(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Saturate expression");
    Require(utilityResult.shader.source.find("fract(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Frac expression");
    Require(utilityResult.shader.source.find("sqrt(max(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Sqrt expression");
    Require(utilityResult.shader.source.find("floor(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Floor expression");
    Require(utilityResult.shader.source.find("ceil(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Ceil expression");
    Require(utilityResult.shader.source.find("sin(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Sin expression");
    Require(utilityResult.shader.source.find("cos(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Cos expression");

    RenderMaterialGraphDocument vectorGraph = MakeDefaultRenderMaterialGraphDocument();
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0" },
    });
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = 120,
        .positionY = 160,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5 0 0" },
    });
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::DotProduct, .positionX = 300, .positionY = 120 });
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 5U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = 120,
        .positionY = 240,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0" },
    });
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 6U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = 120,
        .positionY = 320,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 1 0" },
    });
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 7U, .kind = RenderMaterialGraphNodeKind::CrossProduct, .positionX = 300, .positionY = 280 });
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 8U, .kind = RenderMaterialGraphNodeKind::Normalize, .positionX = 460, .positionY = 280 });
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 9U, .kind = RenderMaterialGraphNodeKind::Length, .positionX = 620, .positionY = 280 });
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 10U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = 120,
        .positionY = 400,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 0" },
    });
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 11U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = 120,
        .positionY = 480,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 0.25" },
    });
    vectorGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 12U, .kind = RenderMaterialGraphNodeKind::Distance, .positionX = 300, .positionY = 440 });
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 2U, "xyz", RenderMaterialGraphNodeKind::DotProduct, 4U, "a"));
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 3U, "xyz", RenderMaterialGraphNodeKind::DotProduct, 4U, "b"));
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::DotProduct, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 5U, "xyz", RenderMaterialGraphNodeKind::CrossProduct, 7U, "a"));
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 6U, "xyz", RenderMaterialGraphNodeKind::CrossProduct, 7U, "b"));
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::CrossProduct, 7U, "value", RenderMaterialGraphNodeKind::Normalize, 8U, "value"));
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Normalize, 8U, "value", RenderMaterialGraphNodeKind::Length, 9U, "value"));
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Length, 9U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 10U, "xyz", RenderMaterialGraphNodeKind::Distance, 12U, "a"));
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 11U, "xyz", RenderMaterialGraphNodeKind::Distance, 12U, "b"));
    vectorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Distance, 12U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"));
    const RenderMaterialGraphCompileResult vectorResult = CompileRenderMaterialGraphToShaderSource(
        vectorGraph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/CompiledVectorMath.kbmat",
        });
    Require(vectorResult.Succeeded(), "KBMAT-GRAPH-0202: Vector math graph should compile to shader source");
    Require(vectorResult.shader.source.find("dot(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit DotProduct expression");
    Require(vectorResult.shader.source.find("cross(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit CrossProduct expression");
    Require(vectorResult.shader.source.find("normalize(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Normalize expression");
    Require(vectorResult.shader.source.find("length(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Length expression");
    Require(vectorResult.shader.source.find("distance(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Distance expression");

    RenderMaterialGraphDocument channelGraph = MakeDefaultRenderMaterialGraphDocument();
    channelGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.2 0.4 0.6 0.8" },
    });
    channelGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::BreakVector, .positionX = 300, .positionY = 80 });
    channelGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::MakeVector, .positionX = 480, .positionY = 80 });
    channelGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::BreakVector, 3U, "value"));
    channelGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "z", RenderMaterialGraphNodeKind::MakeVector, 4U, "x"));
    channelGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "y", RenderMaterialGraphNodeKind::MakeVector, 4U, "y"));
    channelGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "x", RenderMaterialGraphNodeKind::MakeVector, 4U, "z"));
    channelGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::BreakVector, 3U, "w", RenderMaterialGraphNodeKind::MakeVector, 4U, "w"));
    channelGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::MakeVector, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult channelResult = CompileRenderMaterialGraphToShaderSource(
        channelGraph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/CompiledChannelUtility.kbmat",
        });
    Require(channelResult.Succeeded(), "KBMAT-GRAPH-0202: Channel utility graph should compile to shader source");
    Require(channelResult.shader.source.find("vec4(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit MakeVector expression");
    Require(channelResult.shader.source.find(".z") != std::string::npos &&
            channelResult.shader.source.find(".y") != std::string::npos &&
            channelResult.shader.source.find(".x") != std::string::npos &&
            channelResult.shader.source.find(".w") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit BreakVector channel expressions");

    RenderMaterialGraphDocument conditionalGraph = MakeDefaultRenderMaterialGraphDocument();
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 60,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 1 1 1" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 160,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 240,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.75" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::Step, .positionX = 320, .positionY = 180 });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 6U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 340,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 7U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 420,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 8U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 500,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 9U, .kind = RenderMaterialGraphNodeKind::SmoothStep, .positionX = 320, .positionY = 420 });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 10U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 520,
        .positionY = 160,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.25" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 11U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 520,
        .positionY = 240,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 12U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 520,
        .positionY = 320,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.2" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 13U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 520,
        .positionY = 400,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.4" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 14U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 520,
        .positionY = 480,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.8" },
    });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 15U, .kind = RenderMaterialGraphNodeKind::If, .positionX = 720, .positionY = 300 });
    conditionalGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 16U, .kind = RenderMaterialGraphNodeKind::RuntimeSwitch, .positionX = 720, .positionY = 480 });
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::Step, 5U, "edge"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Step, 5U, "value"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Step, 5U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::SmoothStep, 9U, "min"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::SmoothStep, 9U, "max"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::SmoothStep, 9U, "value"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::SmoothStep, 9U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 10U, "value", RenderMaterialGraphNodeKind::If, 15U, "a"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 11U, "value", RenderMaterialGraphNodeKind::If, 15U, "b"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 12U, "value", RenderMaterialGraphNodeKind::If, 15U, "less"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::If, 15U, "equal"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 14U, "value", RenderMaterialGraphNodeKind::If, 15U, "greater"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::If, 15U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 16U, "index"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 12U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 16U, "default"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 16U, "case0"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 14U, "value", RenderMaterialGraphNodeKind::RuntimeSwitch, 16U, "case1"));
    conditionalGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::RuntimeSwitch, 16U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));
    const RenderMaterialGraphCompileResult conditionalResult = CompileRenderMaterialGraphToShaderSource(
        conditionalGraph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/CompiledConditionalMath.kbmat",
        });
    Require(conditionalResult.Succeeded(), "KBMAT-GRAPH-0202: Conditional math graph should compile to shader source");
    Require(conditionalResult.shader.source.find("step(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Step expression");
    Require(conditionalResult.shader.source.find("smoothstep(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit SmoothStep expression");
    Require(conditionalResult.shader.source.find("abs(") != std::string::npos &&
            conditionalResult.shader.source.find("?") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit If compare expression");
    Require(conditionalResult.shader.source.find("kbSwitch4(") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit runtime Switch expression");

    RenderMaterialGraphDocument surfaceGraph = MakeDefaultRenderMaterialGraphDocument();
    surfaceGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" },
    });
    surfaceGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 180,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1" },
    });
    surfaceGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::Desaturate, .positionX = 320, .positionY = 110 });
    surfaceGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 5U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = 120,
        .positionY = 300,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 1" },
    });
    surfaceGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 6U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = 120,
        .positionY = 400,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 -1" },
    });
    surfaceGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 7U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 500,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "2" },
    });
    surfaceGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 8U, .kind = RenderMaterialGraphNodeKind::Fresnel, .positionX = 320, .positionY = 380 });
    surfaceGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::Desaturate, 4U, "color"));
    surfaceGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::Desaturate, 4U, "fraction"));
    surfaceGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Desaturate, 4U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    surfaceGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 5U, "xyz", RenderMaterialGraphNodeKind::Fresnel, 8U, "normal"));
    surfaceGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 6U, "xyz", RenderMaterialGraphNodeKind::Fresnel, 8U, "view"));
    surfaceGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 7U, "value", RenderMaterialGraphNodeKind::Fresnel, 8U, "exponent"));
    surfaceGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Fresnel, 8U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    const RenderMaterialGraphCompileResult surfaceResult = CompileRenderMaterialGraphToShaderSource(
        surfaceGraph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/CompiledSurfaceUtility.kbmat",
        });
    Require(surfaceResult.Succeeded(), "KBMAT-GRAPH-0202: Surface utility graph should compile to shader source");
    Require(surfaceResult.shader.source.find("vec3(0.299, 0.587, 0.114)") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Desaturate luminance expression");
    Require(surfaceResult.shader.source.find("pow(1.0 -") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should emit Fresnel falloff expression");

    RenderMaterialGraphDocument advancedMathGraph = MakeDefaultRenderMaterialGraphDocument();
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "-0.25" } });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::Negate });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.6" } });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::Round });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 6U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.75" } });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 7U, .kind = RenderMaterialGraphNodeKind::Truncate });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 8U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5" } });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 9U, .kind = RenderMaterialGraphNodeKind::Sign });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 10U, .kind = RenderMaterialGraphNodeKind::MakeVector });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 11U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0" } });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 12U, .kind = RenderMaterialGraphNodeKind::Tangent });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 13U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5" } });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 14U, .kind = RenderMaterialGraphNodeKind::ArcSine });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 15U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5" } });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 16U, .kind = RenderMaterialGraphNodeKind::ArcCosine });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 17U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1" } });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 18U, .kind = RenderMaterialGraphNodeKind::ArcTangent });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 19U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1" } });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 20U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1" } });
    advancedMathGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 21U, .kind = RenderMaterialGraphNodeKind::ArcTangent2 });
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::Negate, 3U, "value"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Negate, 3U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "x"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::Round, 5U, "value"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Round, 5U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "y"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::Truncate, 7U, "value"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Truncate, 7U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "z"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::Sign, 9U, "value"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Sign, 9U, "value", RenderMaterialGraphNodeKind::MakeVector, 10U, "w"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::MakeVector, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 11U, "value", RenderMaterialGraphNodeKind::Tangent, 12U, "value"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Tangent, 12U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 13U, "value", RenderMaterialGraphNodeKind::ArcSine, 14U, "value"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ArcSine, 14U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 15U, "value", RenderMaterialGraphNodeKind::ArcCosine, 16U, "value"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ArcCosine, 16U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 17U, "value", RenderMaterialGraphNodeKind::ArcTangent, 18U, "value"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangent, 18U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 19U, "value", RenderMaterialGraphNodeKind::ArcTangent2, 21U, "y"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 20U, "value", RenderMaterialGraphNodeKind::ArcTangent2, 21U, "x"));
    advancedMathGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangent2, 21U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));
    const RenderMaterialGraphCompileResult advancedMathResult = CompileRenderMaterialGraphToShaderSource(
        advancedMathGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0204U, .sourcePath = "/Game/Materials/CompiledAdvancedMath.kbmat" });
    Require(advancedMathResult.Succeeded(), "KBMAT-GRAPH-0202: Advanced math graph should compile to shader source");
    Require(advancedMathResult.shader.source.find("sign(") != std::string::npos, "KBMAT-GRAPH-0202: Shader compiler should emit Sign expression");
    Require(advancedMathResult.shader.source.find("round(") != std::string::npos, "KBMAT-GRAPH-0202: Shader compiler should emit Round expression");
    Require(advancedMathResult.shader.source.find("floor(abs(") != std::string::npos, "KBMAT-GRAPH-0202: Shader compiler should emit Truncate expression");
    Require(advancedMathResult.shader.source.find("tan(") != std::string::npos, "KBMAT-GRAPH-0202: Shader compiler should emit Tangent expression");
    Require(advancedMathResult.shader.source.find("asin(") != std::string::npos, "KBMAT-GRAPH-0202: Shader compiler should emit ArcSine expression");
    Require(advancedMathResult.shader.source.find("acos(") != std::string::npos, "KBMAT-GRAPH-0202: Shader compiler should emit ArcCosine expression");
    Require(advancedMathResult.shader.source.find("atan(") != std::string::npos, "KBMAT-GRAPH-0202: Shader compiler should emit ArcTangent expression");

    RenderMaterialGraphDocument fastTrigGraph = MakeDefaultRenderMaterialGraphDocument();
    fastTrigGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5" } });
    fastTrigGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ArcSineFast });
    fastTrigGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5" } });
    fastTrigGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::ArcCosineFast });
    fastTrigGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 6U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1" } });
    fastTrigGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 7U, .kind = RenderMaterialGraphNodeKind::ArcTangentFast });
    fastTrigGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 8U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1" } });
    fastTrigGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 9U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1" } });
    fastTrigGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 10U, .kind = RenderMaterialGraphNodeKind::ArcTangent2Fast });
    fastTrigGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::ArcSineFast, 3U, "value"));
    fastTrigGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ArcSineFast, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
    fastTrigGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 4U, "value", RenderMaterialGraphNodeKind::ArcCosineFast, 5U, "value"));
    fastTrigGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ArcCosineFast, 5U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));
    fastTrigGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 6U, "value", RenderMaterialGraphNodeKind::ArcTangentFast, 7U, "value"));
    fastTrigGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangentFast, 7U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "occlusion"));
    fastTrigGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 8U, "value", RenderMaterialGraphNodeKind::ArcTangent2Fast, 10U, "y"));
    fastTrigGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 9U, "value", RenderMaterialGraphNodeKind::ArcTangent2Fast, 10U, "x"));
    fastTrigGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ArcTangent2Fast, 10U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));
    const RenderMaterialGraphCompileResult fastTrigResult = CompileRenderMaterialGraphToShaderSource(
        fastTrigGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0205U, .sourcePath = "/Game/Materials/CompiledFastTrig.kbmat" });
    Require(fastTrigResult.Succeeded(), "KBMAT-GRAPH-0202: Fast trig graph should compile to shader source");
    const auto countFastTrigSymbol = [](std::string_view source, std::string_view symbol) noexcept {
        std::uint32_t count = 0U;
        std::size_t offset = 0U;
        while ((offset = source.find(symbol, offset)) != std::string_view::npos) {
            ++count;
            offset += symbol.size();
        }
        return count;
    };
    Require(fastTrigResult.shader.source.find("vec4 kbAtanFast") != std::string::npos, "KBMAT-GRAPH-0202: Fast trig shader should emit the shared atan helper");
    Require(countFastTrigSymbol(fastTrigResult.shader.source, "kbAsinFast(") >= 3U, "KBMAT-GRAPH-0202: Fast trig shader should emit ArcSineFast codegen");
    Require(countFastTrigSymbol(fastTrigResult.shader.source, "kbAcosFast(") >= 2U, "KBMAT-GRAPH-0202: Fast trig shader should emit ArcCosineFast codegen");
    Require(countFastTrigSymbol(fastTrigResult.shader.source, "kbAtanFast(") >= 3U, "KBMAT-GRAPH-0202: Fast trig shader should emit ArcTangentFast codegen");
    Require(countFastTrigSymbol(fastTrigResult.shader.source, "kbAtan2Fast(") >= 3U, "KBMAT-GRAPH-0202: Fast trig shader should emit ArcTangent2Fast codegen");

    RenderMaterialGraphDocument textureGraph = MakeDefaultRenderMaterialGraphDocument();
    textureGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "textureSample2",
            .displayName = "Texture Sample 2",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        },
    });
    textureGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult textureResult = CompileRenderMaterialGraphToShaderSource(
        textureGraph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/InlineTextureSample.kbmat",
        });
    Require(textureResult.Succeeded(), "KBMAT-GRAPH-0202: TextureSample with an inline texture slot should compile");
    Require(textureResult.shader.source.find("u_textureSample2_texture") != std::string::npos,
        "KBMAT-GRAPH-0202: TextureSample inline slot should generate a texture uniform");

    RenderMaterialGraphDocument legacyTextureGraph = MakeDefaultRenderMaterialGraphDocument();
    legacyTextureGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 120,
        .positionY = 80,
    });
    legacyTextureGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult legacyTextureResult = CompileRenderMaterialGraphToShaderSource(
        legacyTextureGraph,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0202U,
            .sourcePath = "/Game/Materials/LegacyInlineTextureSample.kbmat",
        });
    Require(legacyTextureResult.Succeeded(), "KBMAT-GRAPH-0202: legacy TextureSample without stored metadata should use inline texture slot defaults");
    Require(legacyTextureResult.shader.source.find("u_textureSample3_texture") != std::string::npos,
        "KBMAT-GRAPH-0202: legacy TextureSample inline slot should generate a stable texture uniform");
}

void RunMaterialGraphOrganizationNodeCodegenTest() {
    const auto makeColorNode = [](std::uint32_t id, std::string_view hint) {
        RenderMaterialGraphNode node{
            .id = id,
            .kind = RenderMaterialGraphNodeKind::ConstantColor,
            .positionX = 80,
            .positionY = 100,
        };
        node.parameter.defaultValueHint = std::string{ hint };
        return node;
    };

    const auto compileGraph = [](const RenderMaterialGraphDocument& graph, std::uint64_t assetId, const char* message) {
        const RenderMaterialGraphCompileResult compiled =
            CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = assetId });
        std::string failure{ message };
        if (!compiled.Succeeded() && !compiled.diagnostics.empty()) {
            failure += " (first diagnostic: " + compiled.diagnostics.front().message + ")";
        }
        Require(compiled.Succeeded(), failure.c_str());
        return compiled;
    };

    RenderMaterialGraphDocument direct = MakeDefaultRenderMaterialGraphDocument();
    const RenderMaterialGraphNode directColor = makeColorNode(2U, "0.25 0.5 0.75 1");
    direct.nodes.push_back(directColor);
    direct.links.push_back(MakeGraphLink(directColor, "rgba", direct.nodes.front(), "baseColor"));
    const RenderMaterialGraphCompileResult directCompile = compileGraph(direct, 0x5600U, "KBMAT-MAT56: Direct color graph should compile");

    RenderMaterialGraphNode reroute{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::Reroute,
        .positionX = 240,
        .positionY = 100,
        .parameter = RenderMaterialGraphParameterMetadata{ .displayName = "Reroute", .defaultValueHint = "color" },
    };
    Require(IsRenderMaterialGraphInputPin(reroute, "input") && IsRenderMaterialGraphOutputPin(reroute, "output"),
        "KBMAT-MAT56: Reroute node must expose input/output pins");
    Require(RenderMaterialGraphPinDataType(reroute, "input", false) == RenderMaterialGraphPinType::Color &&
            RenderMaterialGraphPinDataType(reroute, "output", true) == RenderMaterialGraphPinType::Color,
        "KBMAT-MAT56: Reroute node must use its authored pass-through pin type");

    RenderMaterialGraphDocument rerouteGraph = MakeDefaultRenderMaterialGraphDocument();
    const RenderMaterialGraphNode rerouteColor = makeColorNode(2U, "0.25 0.5 0.75 1");
    rerouteGraph.nodes.push_back(rerouteColor);
    rerouteGraph.nodes.push_back(reroute);
    rerouteGraph.links.push_back(MakeGraphLink(rerouteColor, "rgba", reroute, "input"));
    rerouteGraph.links.push_back(MakeGraphLink(reroute, "output", rerouteGraph.nodes.front(), "baseColor"));
    const RenderMaterialGraphCompileResult rerouteCompile = compileGraph(rerouteGraph, 0x5601U, "KBMAT-MAT56: Reroute graph should compile");
    Require(rerouteCompile.shader.source == directCompile.shader.source &&
            rerouteCompile.shader.sourceHash == directCompile.shader.sourceHash,
        "KBMAT-MAT56: Reroute must be a zero-cost pass-through that does not change generated shader source");

    RenderMaterialGraphNode declaration{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::NamedRerouteDeclaration,
        .positionX = 260,
        .positionY = 120,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "SurfaceTint", .displayName = "Surface Tint", .defaultValueHint = "color" },
    };
    RenderMaterialGraphNode usage{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::NamedRerouteUsage,
        .positionX = 500,
        .positionY = 120,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "SurfaceTint", .displayName = "Surface Tint", .defaultValueHint = "color" },
    };
    RenderMaterialGraphDocument namedGraph = MakeDefaultRenderMaterialGraphDocument();
    const RenderMaterialGraphNode namedColor = makeColorNode(2U, "0.25 0.5 0.75 1");
    namedGraph.nodes.push_back(namedColor);
    namedGraph.nodes.push_back(declaration);
    namedGraph.nodes.push_back(usage);
    namedGraph.links.push_back(MakeGraphLink(namedColor, "rgba", declaration, "input"));
    namedGraph.links.push_back(MakeGraphLink(usage, "output", namedGraph.nodes.front(), "baseColor"));
    const RenderMaterialGraphCompileResult namedCompile = compileGraph(namedGraph, 0x5602U, "KBMAT-MAT56: Named reroute graph should compile");
    Require(namedCompile.shader.source == directCompile.shader.source,
        "KBMAT-MAT56: Named reroute declaration/usage must inline to the declaration input without changing shader source");

    RenderMaterialGraphDocument missingDeclarationGraph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode missingUsage = usage;
    missingUsage.parameter.stableId = "MissingRoute";
    missingDeclarationGraph.nodes.push_back(missingUsage);
    missingDeclarationGraph.links.push_back(MakeGraphLink(missingUsage, "output", missingDeclarationGraph.nodes.front(), "baseColor"));
    const std::vector<RenderMaterialGraphDiagnostic> missingDeclarationDiagnostics =
        ValidateRenderMaterialGraphDocument(missingDeclarationGraph);
    Require(HasGraphDiagnostic(missingDeclarationDiagnostics, RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed),
        "KBMAT-MAT56: Named reroute usage without a matching declaration must report a graph diagnostic");

    RenderMaterialGraphNode compositeInput{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::CompositeInput,
        .positionX = 220,
        .positionY = 100,
        .parameter = RenderMaterialGraphParameterMetadata{ .displayName = "Composite In", .defaultValueHint = "color" },
    };
    RenderMaterialGraphNode compositeOutput{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::CompositeOutput,
        .positionX = 440,
        .positionY = 100,
        .parameter = RenderMaterialGraphParameterMetadata{ .displayName = "Composite Out", .defaultValueHint = "color" },
    };
    RenderMaterialGraphDocument compositeGraph = MakeDefaultRenderMaterialGraphDocument();
    const RenderMaterialGraphNode compositeColor = makeColorNode(2U, "0.25 0.5 0.75 1");
    compositeGraph.nodes.push_back(compositeColor);
    compositeGraph.nodes.push_back(compositeInput);
    compositeGraph.nodes.push_back(compositeOutput);
    compositeGraph.composites.push_back(RenderMaterialGraphCompositeSubgraph{
        .id = 5U,
        .positionX = 180,
        .positionY = 40,
        .width = 360,
        .height = 180,
        .color = 0x425B4AU,
        .collapsed = true,
        .name = "Nested Surface",
        .nodeIds = { compositeInput.id, compositeOutput.id },
    });
    compositeGraph.links.push_back(MakeGraphLink(compositeColor, "rgba", compositeInput, "input"));
    compositeGraph.links.push_back(MakeGraphLink(compositeInput, "output", compositeOutput, "input"));
    compositeGraph.links.push_back(MakeGraphLink(compositeOutput, "output", compositeGraph.nodes.front(), "baseColor"));
    const RenderMaterialGraphCompileResult compositeCompile = compileGraph(compositeGraph, 0x5603U, "KBMAT-MAT56: Composite tunnel graph should compile");
    Require(compositeCompile.shader.source == directCompile.shader.source,
        "KBMAT-MAT56: Composite input/output tunnels must inline without adding shader code");

    RenderMaterialAssetData asset{};
    asset.graph = compositeGraph;
    std::ostringstream serialized;
    RenderMaterialAssetWriter::Write(serialized, asset);
    const std::string expectedComposite = "graphComposite 5 180 40 360 180 " +
        std::to_string(0x425B4AU) + " true Nested%20Surface 3,4\n";
    Require(serialized.str().find(expectedComposite) != std::string::npos,
        "KBMAT-MAT56: Composite subgraph metadata should serialize with collapsed state and node tunneling ids");

    std::istringstream input{ serialized.str() };
    const RenderMaterialAssetParseResult parsed = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(parsed.Succeeded() && parsed.asset.has_value() && parsed.asset->graph.composites.size() == 1U,
        "KBMAT-MAT56: Composite subgraph metadata should parse after serialization");
    const RenderMaterialGraphCompositeSubgraph& parsedComposite = parsed.asset->graph.composites.front();
    Require(parsedComposite.id == 5U && parsedComposite.collapsed && parsedComposite.name == "Nested Surface" &&
            parsedComposite.nodeIds.size() == 2U && parsedComposite.nodeIds[0] == 3U && parsedComposite.nodeIds[1] == 4U,
        "KBMAT-MAT56: Composite subgraph metadata should round-trip deterministically");
}

void RunMaterialGraphFunctionInliningTest() {
    constexpr std::uint64_t kFunctionAssetId = 0x42420001ULL;

    const auto makeFunctionCall = [](std::uint32_t id, std::uint64_t assetId) {
        return RenderMaterialGraphNode{
            .id = id,
            .kind = RenderMaterialGraphNodeKind::MaterialFunctionCall,
            .positionX = 240,
            .positionY = 96,
            .parameter = RenderMaterialGraphParameterMetadata{
                .stableId = std::to_string(assetId),
                .displayName = "Tint Function",
                .description = "Reusable tint material function",
            },
            .customCode = RenderMaterialGraphCustomCode{
                .body = {},
                .outputType = RenderMaterialGraphPinType::Color,
                .inputs = {
                    RenderMaterialGraphCustomPin{ .name = "Input", .type = RenderMaterialGraphPinType::Color },
                },
                .outputs = {
                    RenderMaterialGraphCustomPin{ .name = "Output", .type = RenderMaterialGraphPinType::Color },
                },
            },
        };
    };

    const auto makeFunctionGraph = [] {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.storageModel = "material-function-asset";
        graph.shadingModel = "unlit";
        graph.nodes.clear();
        RenderMaterialGraphNode input{
            .id = 1U,
            .kind = RenderMaterialGraphNodeKind::FunctionInput,
            .positionX = -360,
            .positionY = 80,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Input", .displayName = "Input", .defaultValueHint = "color" },
        };
        RenderMaterialGraphNode tint{
            .id = 2U,
            .kind = RenderMaterialGraphNodeKind::ConstantColor,
            .positionX = -120,
            .positionY = 160,
            .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.25 0.5 0.75 1" },
        };
        RenderMaterialGraphNode multiply{
            .id = 3U,
            .kind = RenderMaterialGraphNodeKind::Multiply,
            .positionX = 80,
            .positionY = 120,
        };
        RenderMaterialGraphNode output{
            .id = 4U,
            .kind = RenderMaterialGraphNodeKind::FunctionOutput,
            .positionX = 320,
            .positionY = 120,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Output", .displayName = "Output", .defaultValueHint = "color" },
        };
        graph.nodes.push_back(input);
        graph.nodes.push_back(tint);
        graph.nodes.push_back(multiply);
        graph.nodes.push_back(output);
        graph.links.push_back(MakeGraphLink(input, "value", multiply, "a"));
        graph.links.push_back(MakeGraphLink(tint, "rgba", multiply, "b"));
        graph.links.push_back(MakeGraphLink(multiply, "value", output, "value"));
        return graph;
    };

    const auto makeMaterialGraphWithCall = [&makeFunctionCall](std::uint32_t callId, std::uint32_t colorId) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        RenderMaterialGraphNode source{
            .id = colorId,
            .kind = RenderMaterialGraphNodeKind::ConstantColor,
            .positionX = -160,
            .positionY = 96,
            .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.8 0.8 0.8 1" },
        };
        const RenderMaterialGraphNode call = makeFunctionCall(callId, kFunctionAssetId);
        const RenderMaterialGraphNode output = graph.nodes.front();
        graph.nodes.push_back(source);
        graph.nodes.push_back(call);
        graph.links.push_back(MakeGraphLink(source, "rgba", call, "Input"));
        graph.links.push_back(MakeGraphLink(call, "Output", output, "baseColor"));
        return graph;
    };

    const auto makeExpandedGraph = [] {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        RenderMaterialGraphNode source{
            .id = 2U,
            .kind = RenderMaterialGraphNodeKind::ConstantColor,
            .positionX = -160,
            .positionY = 96,
            .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.8 0.8 0.8 1" },
        };
        RenderMaterialGraphNode tint{
            .id = 3U,
            .kind = RenderMaterialGraphNodeKind::ConstantColor,
            .positionX = 120,
            .positionY = 256,
            .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.25 0.5 0.75 1" },
        };
        RenderMaterialGraphNode multiply{
            .id = 4U,
            .kind = RenderMaterialGraphNodeKind::Multiply,
            .positionX = 320,
            .positionY = 216,
        };
        const RenderMaterialGraphNode output = graph.nodes.front();
        graph.nodes.push_back(source);
        graph.nodes.push_back(tint);
        graph.nodes.push_back(multiply);
        graph.links.push_back(MakeGraphLink(source, "rgba", multiply, "a"));
        graph.links.push_back(MakeGraphLink(tint, "rgba", multiply, "b"));
        graph.links.push_back(MakeGraphLink(multiply, "value", output, "baseColor"));
        return graph;
    };

    RenderMaterialGraphDocument functionGraph = makeFunctionGraph();
    const RenderMaterialGraphNode* functionInput = FindRenderMaterialGraphNode(functionGraph, 1U);
    const RenderMaterialGraphNode* functionOutput = FindRenderMaterialGraphNode(functionGraph, 4U);
    Require(functionInput != nullptr && functionOutput != nullptr &&
            IsRenderMaterialGraphOutputPin(*functionInput, "value") &&
            IsRenderMaterialGraphInputPin(*functionOutput, "value") &&
            RenderMaterialGraphPinDataType(*functionInput, "value", true) == RenderMaterialGraphPinType::Color,
        "KBMAT-MAT42: FunctionInput/FunctionOutput must expose typed endpoint pins");

    RenderMaterialFunctionAssetData functionAsset{ .graph = functionGraph };
    const std::filesystem::path functionPath = std::filesystem::temp_directory_path() / "kbmat_mat42_roundtrip.kbmatfn";
    std::error_code fileError;
    std::filesystem::remove(functionPath, fileError);
    Require(RenderMaterialFunctionAssetLoader::SaveFunction(functionPath, functionAsset),
        "KBMAT-MAT42: RenderMaterialFunction asset must save to disk");
    const std::optional<RenderMaterialFunctionAssetData> loadedFunction = RenderMaterialFunctionAssetLoader::LoadFunction(functionPath);
    Require(loadedFunction.has_value() && loadedFunction->graph.storageModel == "material-function-asset" &&
            FindRenderMaterialGraphNode(loadedFunction->graph, 1U) != nullptr &&
            FindRenderMaterialGraphNode(loadedFunction->graph, 4U) != nullptr,
        "KBMAT-MAT42: RenderMaterialFunction asset must load with its endpoint graph intact");
    std::filesystem::remove(functionPath, fileError);

    RenderMaterialGraphFunctionLibrary library{};
    library.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{
        .assetId = kFunctionAssetId,
        .contentHash = 0x11110001ULL,
        .name = "/Game/Functions/Tint.kbmatfn",
        .graph = functionGraph,
    });

    RenderMaterialGraphDocument materialGraph = makeMaterialGraphWithCall(3U, 2U);
    const RenderMaterialGraphNode* callNode = FindRenderMaterialGraphNode(materialGraph, 3U);
    Require(callNode != nullptr &&
            RenderMaterialGraphNodeInputPinNames(*callNode).size() == 1U &&
            RenderMaterialGraphNodeInputPinNames(*callNode)[0] == "Input" &&
            RenderMaterialGraphNodeOutputPinNames(*callNode).size() == 1U &&
            RenderMaterialGraphNodeOutputPinNames(*callNode)[0] == "Output" &&
            IsRenderMaterialGraphInputPin(*callNode, "Input") &&
            IsRenderMaterialGraphOutputPin(*callNode, "Output"),
        "KBMAT-MAT42: MaterialFunctionCall must expose node-authored dynamic pins");

    const std::vector<std::uint64_t> dependencies = DiscoverRenderMaterialGraphFunctionDependencies(materialGraph);
    Require(dependencies.size() == 1U && dependencies[0] == kFunctionAssetId,
        "KBMAT-MAT42: Function dependency discovery must report the called function asset once");

    RenderMaterialGraphBuildContext context{ .assetId = 0x4200U, .functionLibrary = &library };
    const RenderMaterialGraphFunctionInlineResult inlined = InlineRenderMaterialGraphFunctions(materialGraph, context);
    Require(inlined.Succeeded(), "KBMAT-MAT42: MaterialFunctionCall must inline with a populated function library");
    for (const RenderMaterialGraphNode& node : inlined.graph.nodes) {
        Require(node.kind != RenderMaterialGraphNodeKind::MaterialFunctionCall &&
                node.kind != RenderMaterialGraphNodeKind::FunctionInput &&
                node.kind != RenderMaterialGraphNodeKind::FunctionOutput,
            "KBMAT-MAT42: Inlined material graph must not leave function endpoint or call nodes behind");
    }

    const RenderMaterialGraphCompileResult functionCompile = CompileRenderMaterialGraphToShaderSource(materialGraph, context);
    const RenderMaterialGraphCompileResult expandedCompile = CompileRenderMaterialGraphToShaderSource(
        makeExpandedGraph(),
        RenderMaterialGraphBuildContext{ .assetId = 0x4201U });
    Require(functionCompile.Succeeded() && expandedCompile.Succeeded(),
        "KBMAT-MAT42: Function-backed and manually-expanded graphs must both compile");
    Require(functionCompile.shader.source == expandedCompile.shader.source &&
            functionCompile.shader.sourceHash == expandedCompile.shader.sourceHash,
        "KBMAT-MAT42: Function inlining must produce the same shader source/hash as the manually expanded graph");

    RenderMaterialGraphDocument reuseGraph = makeMaterialGraphWithCall(3U, 2U);
    RenderMaterialGraphNode secondSource{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = -160,
        .positionY = 220,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.2 0.7 0.4 1" },
    };
    RenderMaterialGraphNode secondCall = makeFunctionCall(5U, kFunctionAssetId);
    secondCall.positionY = 220;
    const RenderMaterialGraphNode reuseOutput = reuseGraph.nodes.front();
    reuseGraph.nodes.push_back(secondSource);
    reuseGraph.nodes.push_back(secondCall);
    reuseGraph.links.push_back(MakeGraphLink(secondSource, "rgba", secondCall, "Input"));
    reuseGraph.links.push_back(MakeGraphLink(secondCall, "Output", reuseOutput, "emissive"));
    const std::vector<std::uint64_t> reuseDependencies = DiscoverRenderMaterialGraphFunctionDependencies(reuseGraph);
    const RenderMaterialGraphCompileResult reuseCompile = CompileRenderMaterialGraphToShaderSource(
        reuseGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x4202U, .functionLibrary = &library });
    Require(reuseDependencies.size() == 1U && reuseDependencies[0] == kFunctionAssetId && reuseCompile.Succeeded(),
        "KBMAT-MAT42: Multiple calls to the same function must reuse one dependency entry and compile");

    RenderMaterialGraphCompileArtifactCache cache;
    RenderMaterialGraphCompileArtifactCacheResult firstCache =
        CompileRenderMaterialGraphWithArtifactCache(cache, materialGraph, context);
    Require(firstCache.compile.Succeeded() && !firstCache.cacheHit && cache.Size() == 1U,
        "KBMAT-MAT42: First function-backed compile must populate the artifact cache");
    const RenderMaterialGraphCompileArtifactCacheResult repeatCache =
        CompileRenderMaterialGraphWithArtifactCache(cache, materialGraph, context);
    Require(repeatCache.compile.Succeeded() && repeatCache.cacheHit,
        "KBMAT-MAT42: Unchanged function-backed compile must hit the artifact cache");
    library.entries[0].contentHash = 0x22220002ULL;
    context.functionLibrary = &library;
    const RenderMaterialGraphCompileArtifactCacheKey changedKey =
        BuildRenderMaterialGraphCompileArtifactCacheKey(materialGraph, {}, 0U, context);
    Require(changedKey.dependencyHash != firstCache.key.dependencyHash &&
            changedKey.combinedHash != firstCache.key.combinedHash,
        "KBMAT-MAT42: Changing a material function content hash must invalidate material variant identity");
    const RenderMaterialGraphCompileArtifactCacheResult changedCache =
        CompileRenderMaterialGraphWithArtifactCache(cache, materialGraph, context);
    Require(changedCache.compile.Succeeded() && !changedCache.cacheHit && cache.Size() == 2U,
        "KBMAT-MAT42: Changed material function dependency must miss the prior artifact cache entry");

    const RenderMaterialGraphCompileResult missingLibrary = CompileRenderMaterialGraphToShaderSource(
        materialGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x4203U });
    Require(!missingLibrary.Succeeded() && HasGraphDiagnostic(missingLibrary.diagnostics, RenderMaterialGraphDiagnosticKind::MissingMaterialFunction),
        "KBMAT-MAT42: Calling a function without a function library must report MissingMaterialFunction");

    RenderMaterialGraphFunctionLibrary emptyLibrary{};
    const RenderMaterialGraphCompileResult missingEntry = CompileRenderMaterialGraphToShaderSource(
        materialGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x4204U, .functionLibrary = &emptyLibrary });
    Require(!missingEntry.Succeeded() && HasGraphDiagnostic(missingEntry.diagnostics, RenderMaterialGraphDiagnosticKind::MissingMaterialFunction),
        "KBMAT-MAT42: Calling a missing function asset must report MissingMaterialFunction");

    RenderMaterialGraphDocument mismatchGraph = materialGraph;
    RenderMaterialGraphNode* mismatchCall = nullptr;
    for (RenderMaterialGraphNode& node : mismatchGraph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall) {
            mismatchCall = &node;
            break;
        }
    }
    Require(mismatchCall != nullptr, "KBMAT-MAT42: mismatch fixture must contain a MaterialFunctionCall");
    mismatchCall->customCode.outputs[0].name = "StaleOutput";
    const RenderMaterialGraphCompileResult mismatchCompile = CompileRenderMaterialGraphToShaderSource(
        mismatchGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x4205U, .functionLibrary = &library });
    Require(!mismatchCompile.Succeeded() &&
            HasGraphDiagnostic(mismatchCompile.diagnostics, RenderMaterialGraphDiagnosticKind::MaterialFunctionSignatureMismatch),
        "KBMAT-MAT42: Stale MaterialFunctionCall pins must report MaterialFunctionSignatureMismatch");

    RenderMaterialGraphDocument cyclicFunction = functionGraph;
    const RenderMaterialGraphNode recursiveCall = makeFunctionCall(5U, kFunctionAssetId);
    cyclicFunction.nodes.push_back(recursiveCall);
    RenderMaterialGraphFunctionLibrary cyclicLibrary{};
    cyclicLibrary.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{
        .assetId = kFunctionAssetId,
        .contentHash = 0x33330003ULL,
        .name = "/Game/Functions/RecursiveTint.kbmatfn",
        .graph = cyclicFunction,
    });
    const RenderMaterialGraphCompileResult cycleCompile = CompileRenderMaterialGraphToShaderSource(
        materialGraph,
        RenderMaterialGraphBuildContext{ .assetId = 0x4206U, .functionLibrary = &cyclicLibrary });
    Require(!cycleCompile.Succeeded() &&
            HasGraphDiagnostic(cycleCompile.diagnostics, RenderMaterialGraphDiagnosticKind::MaterialFunctionCycle),
        "KBMAT-MAT42: Recursive function calls must report MaterialFunctionCycle");
}

void RunMaterialGraphLayerStackInliningTest() {
    constexpr std::uint64_t kRedLayerId = 0x43000001ULL;
    constexpr std::uint64_t kBlueLayerId = 0x43000002ULL;
    constexpr std::uint64_t kBlendId = 0x43000003ULL;

    const auto makeLayerFunction = []() {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.storageModel = "material-function-asset";
        graph.shadingModel = "unlit";
        graph.nodes.clear();
        RenderMaterialGraphNode tint{
            .id = 1U,
            .kind = RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Tint", .displayName = "Tint", .defaultValueHint = "color" },
        };
        RenderMaterialGraphNode makeAttributes{
            .id = 2U,
            .kind = RenderMaterialGraphNodeKind::MakeMaterialAttributes,
        };
        RenderMaterialGraphNode output{
            .id = 3U,
            .kind = RenderMaterialGraphNodeKind::FunctionOutput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Attributes", .displayName = "Attributes", .defaultValueHint = "materialAttributes" },
        };
        graph.nodes.push_back(tint);
        graph.nodes.push_back(makeAttributes);
        graph.nodes.push_back(output);
        graph.links.push_back(MakeGraphLink(tint, "value", makeAttributes, "baseColor"));
        graph.links.push_back(MakeGraphLink(makeAttributes, "attributes", output, "value"));
        return graph;
    };

    const auto makeBlendFunction = []() {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.storageModel = "material-function-asset";
        graph.shadingModel = "unlit";
        graph.nodes.clear();
        RenderMaterialGraphNode a{
            .id = 1U,
            .kind = RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "A", .displayName = "A", .defaultValueHint = "materialAttributes" },
        };
        RenderMaterialGraphNode b{
            .id = 2U,
            .kind = RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "B", .displayName = "B", .defaultValueHint = "materialAttributes" },
        };
        RenderMaterialGraphNode factor{
            .id = 3U,
            .kind = RenderMaterialGraphNodeKind::FunctionInput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Factor", .displayName = "Factor", .defaultValueHint = "float" },
        };
        RenderMaterialGraphNode blend{
            .id = 4U,
            .kind = RenderMaterialGraphNodeKind::BlendMaterialAttributes,
        };
        RenderMaterialGraphNode output{
            .id = 5U,
            .kind = RenderMaterialGraphNodeKind::FunctionOutput,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "Attributes", .displayName = "Attributes", .defaultValueHint = "materialAttributes" },
        };
        graph.nodes.push_back(a);
        graph.nodes.push_back(b);
        graph.nodes.push_back(factor);
        graph.nodes.push_back(blend);
        graph.nodes.push_back(output);
        graph.links.push_back(MakeGraphLink(a, "value", blend, "a"));
        graph.links.push_back(MakeGraphLink(b, "value", blend, "b"));
        graph.links.push_back(MakeGraphLink(factor, "value", blend, "factor"));
        graph.links.push_back(MakeGraphLink(blend, "attributes", output, "value"));
        return graph;
    };

    RenderMaterialGraphFunctionLibrary library{};
    library.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{ .assetId = kRedLayerId, .contentHash = 0x1001U, .name = "/Game/Layers/Red.kbmatfn", .graph = makeLayerFunction() });
    library.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{ .assetId = kBlueLayerId, .contentHash = 0x1002U, .name = "/Game/Layers/Blue.kbmatfn", .graph = makeLayerFunction() });
    library.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{ .assetId = kBlendId, .contentHash = 0x1003U, .name = "/Game/Layers/HalfBlend.kbmatfn", .graph = makeBlendFunction() });

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    RenderMaterialGraphNode stack{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::LayerStack,
        .positionX = -240,
        .positionY = 120,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "surfaceLayers", .displayName = "Surface Layers", .description = "Two material layers blended by a material blend function." },
        .layerStack = {
            RenderMaterialGraphLayerStackEntry{
                .layerFunctionAssetId = kRedLayerId,
                .enabled = true,
                .layerName = "Red Base",
                .linkState = "base-linked",
                .layerParameters = {
                    RenderMaterialGraphLayerStackParameter{ .pinName = "Tint", .type = RenderMaterialGraphPinType::Color, .valueHint = "1 0 0 1" },
                },
            },
            RenderMaterialGraphLayerStackEntry{
                .layerFunctionAssetId = kBlueLayerId,
                .blendFunctionAssetId = kBlendId,
                .enabled = true,
                .layerName = "Blue Paint",
                .blendName = "Half Blend",
                .linkState = "paint-linked",
                .layerParameters = {
                    RenderMaterialGraphLayerStackParameter{ .pinName = "Tint", .type = RenderMaterialGraphPinType::Color, .valueHint = "0 0 1 1" },
                },
                .blendParameters = {
                    RenderMaterialGraphLayerStackParameter{ .pinName = "Factor", .type = RenderMaterialGraphPinType::Float, .valueHint = "0.5" },
                },
            },
        },
    };
    const RenderMaterialGraphNode output = graph.nodes.front();
    graph.nodes.push_back(stack);
    graph.links.push_back(MakeGraphLink(stack, "attributes", output, "attributes"));

    Require(IsRenderMaterialGraphOutputPin(stack, "attributes") &&
            RenderMaterialGraphPinDataType(stack, "attributes", true) == RenderMaterialGraphPinType::MaterialAttributes,
        "KBMAT-MAT43: LayerStack must expose a MaterialAttributes output pin");
    const std::vector<std::uint64_t> dependencies = DiscoverRenderMaterialGraphFunctionDependencies(graph);
    Require(dependencies.size() == 3U &&
            dependencies[0] == kRedLayerId &&
            dependencies[1] == kBlueLayerId &&
            dependencies[2] == kBlendId,
        "KBMAT-MAT43: LayerStack dependency discovery must include layer and blend functions once");

    std::ostringstream serialized;
    WriteRenderMaterialGraphDocument(serialized, graph);
    Require(serialized.str().find("graphLayerStackEntry 2 1 " + std::to_string(kBlueLayerId) + " " + std::to_string(kBlendId) + " true Blue%20Paint Half%20Blend paint-linked") != std::string::npos,
        "KBMAT-MAT43: LayerStack entries must serialize layer, blend and link-state data");
    Require(serialized.str().find("graphLayerStackParameter 2 1 layer Tint color 0%200%201%201") != std::string::npos &&
            serialized.str().find("graphLayerStackParameter 2 1 blend Factor float 0.5") != std::string::npos,
        "KBMAT-MAT43: LayerStack entries must serialize per-layer and per-blend parameters");
    RenderMaterialAssetData carrier{};
    carrier.graph = graph;
    std::ostringstream materialSerialized;
    RenderMaterialAssetWriter::Write(materialSerialized, carrier);
    std::istringstream parsedInput{ materialSerialized.str() };
    const RenderMaterialAssetParseResult parsed = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(parsedInput);
    Require(parsed.Succeeded() && parsed.asset.has_value() &&
            parsed.asset->graph.nodes.size() == 2U &&
            parsed.asset->graph.nodes[1].kind == RenderMaterialGraphNodeKind::LayerStack &&
            parsed.asset->graph.nodes[1].layerStack.size() == 2U &&
            parsed.asset->graph.nodes[1].layerStack[1].linkState == "paint-linked" &&
            parsed.asset->graph.nodes[1].layerStack[1].layerParameters.size() == 1U &&
            parsed.asset->graph.nodes[1].layerStack[1].blendParameters.size() == 1U &&
            parsed.asset->graph.nodes[1].layerStack[1].blendParameters[0].pinName == "Factor",
        "KBMAT-MAT43: LayerStack entries must round-trip through material graph serialization");

    const RenderMaterialGraphFunctionInlineResult expanded = InlineRenderMaterialGraphFunctions(
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x4300U, .functionLibrary = &library });
    Require(expanded.Succeeded(), "KBMAT-MAT43: LayerStack must expand and inline with layer/blend function assets");
    for (const RenderMaterialGraphNode& node : expanded.graph.nodes) {
        Require(node.kind != RenderMaterialGraphNodeKind::LayerStack &&
                node.kind != RenderMaterialGraphNodeKind::MaterialFunctionCall &&
                node.kind != RenderMaterialGraphNodeKind::FunctionInput &&
                node.kind != RenderMaterialGraphNodeKind::FunctionOutput,
            "KBMAT-MAT43: LayerStack expansion must not leave stack/function scaffolding in the compiled graph");
    }

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x4301U, .functionLibrary = &library });
    Require(compiled.Succeeded() &&
            compiled.shader.source.find("mix(") != std::string::npos &&
            compiled.shader.source.find("LayerStack") == std::string::npos,
        "KBMAT-MAT43: LayerStack graph must compile to real MaterialAttributes blend shader code");

    const RenderMaterialGraphCompileArtifactCacheKey keyBefore =
        BuildRenderMaterialGraphCompileArtifactCacheKey(graph, {}, 0U, RenderMaterialGraphBuildContext{ .assetId = 0x4302U, .functionLibrary = &library });
    library.entries[0].contentHash = 0x2001U;
    const RenderMaterialGraphCompileArtifactCacheKey keyAfter =
        BuildRenderMaterialGraphCompileArtifactCacheKey(graph, {}, 0U, RenderMaterialGraphBuildContext{ .assetId = 0x4302U, .functionLibrary = &library });
    Require(keyBefore.dependencyHash != keyAfter.dependencyHash && keyBefore.combinedHash != keyAfter.combinedHash,
        "KBMAT-MAT43: Layer function changes must invalidate the layer stack material variant identity");
    RenderMaterialGraphDocument parameterChanged = graph;
    for (RenderMaterialGraphNode& node : parameterChanged.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::LayerStack) {
            node.layerStack[1].blendParameters[0].valueHint = "0.25";
            break;
        }
    }
    const RenderMaterialGraphCompileArtifactCacheKey parameterKey =
        BuildRenderMaterialGraphCompileArtifactCacheKey(parameterChanged, {}, 0U, RenderMaterialGraphBuildContext{ .assetId = 0x4302U, .functionLibrary = &library });
    Require(parameterKey.combinedHash != keyAfter.combinedHash,
        "KBMAT-MAT43: Layer stack parameter changes must invalidate the material variant identity");

    RenderMaterialGraphDocument missingBlend = graph;
    RenderMaterialGraphNode* missingStack = nullptr;
    for (RenderMaterialGraphNode& node : missingBlend.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::LayerStack) {
            missingStack = &node;
            break;
        }
    }
    Require(missingStack != nullptr, "KBMAT-MAT43: missing blend fixture must contain a LayerStack");
    missingStack->layerStack[1].blendFunctionAssetId = 0U;
    const RenderMaterialGraphCompileResult missingBlendCompile = CompileRenderMaterialGraphToShaderSource(
        missingBlend,
        RenderMaterialGraphBuildContext{ .assetId = 0x4303U, .functionLibrary = &library });
    Require(!missingBlendCompile.Succeeded() &&
            HasGraphDiagnostic(missingBlendCompile.diagnostics, RenderMaterialGraphDiagnosticKind::MissingMaterialFunction),
        "KBMAT-MAT43: LayerStack must diagnose missing blend function assets");

    RenderMaterialGraphFunctionLibrary mismatchLibrary = library;
    mismatchLibrary.entries[2].contentHash = 0x3003U;
    mismatchLibrary.entries[2].graph = makeLayerFunction();
    const RenderMaterialGraphCompileResult mismatchCompile = CompileRenderMaterialGraphToShaderSource(
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x4304U, .functionLibrary = &mismatchLibrary });
    Require(!mismatchCompile.Succeeded() &&
            HasGraphDiagnostic(mismatchCompile.diagnostics, RenderMaterialGraphDiagnosticKind::MaterialFunctionSignatureMismatch),
        "KBMAT-MAT43: LayerStack blend functions must validate their MaterialAttributes A/B signature");
}

void RunMaterialGraphMaterialTypeDocumentGenerationTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "tintColor",
            .displayName = "Tint Color",
            .defaultValueHint = "1 1 1 1",
            .editorOrder = 10U,
        },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ParameterTexture,
        .positionX = 120,
        .positionY = 220,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "albedo",
            .displayName = "Albedo",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
            .editorOrder = 20U,
        },
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphMaterialTypeBuildResult result = BuildRenderMaterialGraphMaterialTypeDocument(
        graph,
        "graph.surface.tint",
        7U,
        RenderMaterialGraphBuildContext{
            .assetId = 0x0203U,
            .sourcePath = "/Game/Materials/TintGraph.kbmat",
        });
    Require(result.Succeeded() && result.document.has_value(), "KBMAT-GRAPH-0203: Graph-backed Material Type document should generate from a valid graph");
    const RenderMaterialTypeDocument& document = *result.document;
    Require(document.stableTypeId == "graph.surface.tint" && document.version == 7U,
        "KBMAT-GRAPH-0203: Generated Material Type document should preserve stable id and version");
    Require(document.domain == RenderMaterialDomain::Surface && document.shaderModel == RenderMaterialShaderModel::MetallicRoughnessPbr,
        "KBMAT-GRAPH-0203: Generated Material Type document should declare surface PBR render state");
    Require(document.defaultBlendMode == RenderMaterialBlendMode::Opaque && document.defaultCullMode == RenderMaterialCullMode::BackFace,
        "KBMAT-GRAPH-0203: Generated Material Type document should declare default render state");
    Require(FindMaterialParameterSchema(document.schema, "tintColor") != nullptr,
        "KBMAT-GRAPH-0203: Generated Material Type schema should include graph parameters");
    Require(FindMaterialTextureSlotSchema(document.schema, "albedoTextureAssetId") != nullptr,
        "KBMAT-GRAPH-0203: Generated Material Type schema should include graph texture slots");
    Require(!document.renderPasses.empty() && document.renderPasses[0].fragmentShader.find("graph_fs_") == 0U,
        "KBMAT-GRAPH-0203: Generated Material Type document should reference graph fragment shader source");

    const auto hasPermutation = [&document](std::string_view name) {
        for (const RenderMaterialTypePermutationKey& key : document.permutationKeys) {
            if (key.name == name && !key.allowedValues.empty()) {
                return true;
            }
        }
        return false;
    };
    Require(hasPermutation("alphaMode") && hasPermutation("doubleSided") && hasPermutation("graphSourceHash"),
        "KBMAT-GRAPH-0203: Generated Material Type document should declare permutation keys");

    const auto hasResource = [&document](std::string_view name, std::string_view kind) {
        for (const RenderMaterialTypeRequiredResource& resource : document.requiredResources) {
            if (resource.name == name && resource.kind == kind) {
                return true;
            }
        }
        return false;
    };
    Require(hasResource("vs_mesh_instanced", "vertexShader"),
        "KBMAT-GRAPH-0203: Generated Material Type document should require mesh vertex shader");
    Require(hasResource("albedoTextureAssetId", "texture"),
        "KBMAT-GRAPH-0203: Generated Material Type document should require graph texture slot resource metadata");
}

void RunMaterialGraphCompileArtifactCacheTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 80,
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const std::vector<RenderMaterialGraphDependencyHashInput> dependencies{
        RenderMaterialGraphDependencyHashInput{ .assetId = 10U, .contentHash = 200U, .name = "albedo" },
    };
    RenderMaterialGraphCompileArtifactCache cache;
    RenderMaterialGraphCompileArtifactCacheResult first = CompileRenderMaterialGraphWithArtifactCache(
        cache,
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0204U, .sourcePath = "/Game/Materials/Cached.kbmat" },
        dependencies,
        0xFEEDU);
    Require(first.compile.Succeeded() && !first.cacheHit && cache.Size() == 1U,
        "KBMAT-GRAPH-0204: First graph compile should populate artifact cache");

    RenderMaterialGraphCompileArtifactCacheResult second = CompileRenderMaterialGraphWithArtifactCache(
        cache,
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0204U, .sourcePath = "/Game/Materials/Cached.kbmat" },
        dependencies,
        0xFEEDU);
    Require(second.compile.Succeeded() && second.cacheHit && second.key == first.key,
        "KBMAT-GRAPH-0204: Same graph/dependency/include hashes should hit artifact cache");
    Require(second.compile.shader.sourceHash == first.compile.shader.sourceHash,
        "KBMAT-GRAPH-0204: Cache hit should return cached shader artifact");

    const std::vector<RenderMaterialGraphDependencyHashInput> changedDependencies{
        RenderMaterialGraphDependencyHashInput{ .assetId = 10U, .contentHash = 201U, .name = "albedo" },
    };
    RenderMaterialGraphCompileArtifactCacheResult dependencyMiss = CompileRenderMaterialGraphWithArtifactCache(
        cache,
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0204U, .sourcePath = "/Game/Materials/Cached.kbmat" },
        changedDependencies,
        0xFEEDU);
    Require(dependencyMiss.compile.Succeeded() && !dependencyMiss.cacheHit && dependencyMiss.key.dependencyHash != first.key.dependencyHash,
        "KBMAT-GRAPH-0204: Texture dependency hash change should invalidate cache key");

    Require(cache.Invalidate(first.key), "KBMAT-GRAPH-0204: Explicit cache invalidation by key should remove an artifact");
    RenderMaterialGraphCompileArtifactCacheResult afterInvalidate = CompileRenderMaterialGraphWithArtifactCache(
        cache,
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0204U, .sourcePath = "/Game/Materials/Cached.kbmat" },
        dependencies,
        0xFEEDU);
    Require(afterInvalidate.compile.Succeeded() && !afterInvalidate.cacheHit,
        "KBMAT-GRAPH-0204: Recompile after explicit key invalidation should miss cache");

    const std::size_t removed = cache.InvalidateGraphContentHash(afterInvalidate.key.graphContentHash);
    Require(removed >= 1U, "KBMAT-GRAPH-0204: Explicit graph-content invalidation should remove cached graph artifacts");

    cache.Clear();
    cache.SetCapacity(1U);
    Require(cache.Capacity() == 1U && cache.EvictionCount() == 0U,
        "KBMAT-MAT69: Compile artifact cache must expose its configured capacity before eviction");
    const RenderMaterialGraphCompileArtifactCacheResult budgetFirst = CompileRenderMaterialGraphWithArtifactCache(
        cache,
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0204U, .sourcePath = "/Game/Materials/Cached.kbmat" },
        dependencies,
        0xFEEDU);
    const RenderMaterialGraphCompileArtifactCacheResult budgetSecond = CompileRenderMaterialGraphWithArtifactCache(
        cache,
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0204U, .sourcePath = "/Game/Materials/Cached.kbmat" },
        changedDependencies,
        0xFEEDU);
    Require(budgetFirst.compile.Succeeded() && budgetSecond.compile.Succeeded() &&
            cache.Size() == 1U && cache.EvictionCount() == 1U,
        "KBMAT-MAT69: Compile artifact cache must evict the oldest entry when capacity is exceeded");
    const RenderMaterialGraphCompileArtifactCacheResult budgetRevisit = CompileRenderMaterialGraphWithArtifactCache(
        cache,
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0204U, .sourcePath = "/Game/Materials/Cached.kbmat" },
        dependencies,
        0xFEEDU);
    Require(budgetRevisit.compile.Succeeded() && !budgetRevisit.cacheHit && cache.EvictionCount() == 2U,
        "KBMAT-MAT69: Reusing an evicted graph artifact must rebuild instead of reporting a false cache hit");
}

void RunMaterialGraphCompilerDiagnosticsCoverageTest() {
    RenderMaterialGraphDocument graph{};
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 1U,
        .kind = RenderMaterialGraphNodeKind::MaterialOutput,
        .positionX = 640,
        .positionY = 240,
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = static_cast<RenderMaterialGraphNodeKind>(255U),
        .positionX = 0,
        .positionY = 0,
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ParameterTexture,
        .positionX = 120,
        .positionY = 180,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "badNormal",
            .displayName = "Bad Normal",
            .textureRole = "normal",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
        },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::Add,
        .positionX = 300,
        .positionY = 160,
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 5U,
        .kind = RenderMaterialGraphNodeKind::Multiply,
        .positionX = 460,
        .positionY = 160,
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterTexture, 3U, "texture", RenderMaterialGraphNodeKind::Add, 4U, "b"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Add, 4U, "value", RenderMaterialGraphNodeKind::Multiply, 5U, "a"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Multiply, 5U, "value", RenderMaterialGraphNodeKind::Add, 4U, "a"));

    const RenderMaterialGraphCompileResult compile = CompileRenderMaterialGraphToShaderSource(
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x0207U, .sourcePath = "/Game/Materials/BrokenCompilerDiagnostics.kbmat" });
    Require(!compile.Succeeded(), "KBMAT-GRAPH-0207: Broken graph should fail compiler diagnostics coverage test");
    Require(!HasGraphDiagnostic(compile.diagnostics, RenderMaterialGraphDiagnosticKind::DisconnectedRequiredOutput),
        "KBMAT-GRAPH-0207: Compiler diagnostics should not treat disconnected Material Output BaseColor as an error");
    Require(HasGraphDiagnostic(compile.diagnostics, RenderMaterialGraphDiagnosticKind::TypeMismatch),
        "KBMAT-GRAPH-0207: Compiler diagnostics should cover type mismatch");
    Require(HasGraphDiagnostic(compile.diagnostics, RenderMaterialGraphDiagnosticKind::Cycle),
        "KBMAT-GRAPH-0207: Compiler diagnostics should cover cycle");
    Require(HasGraphDiagnostic(compile.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedNode),
        "KBMAT-GRAPH-0207: Compiler diagnostics should cover unsupported node");
    Require(HasGraphDiagnostic(compile.diagnostics, RenderMaterialGraphDiagnosticKind::InvalidColorSpaceRole),
        "KBMAT-GRAPH-0207: Compiler diagnostics should cover invalid texture role/color-space");

    RenderMaterialGraphDocument inlineTexture = MakeDefaultRenderMaterialGraphDocument();
    inlineTexture.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "textureSample2",
            .displayName = "Texture Sample 2",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        },
    });
    inlineTexture.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult inlineTextureResult = CompileRenderMaterialGraphToShaderSource(
        inlineTexture,
        RenderMaterialGraphBuildContext{ .assetId = 0x0207U, .sourcePath = "/Game/Materials/InlineTextureSample.kbmat" });
    Require(inlineTextureResult.Succeeded(),
        "KBMAT-GRAPH-0207: Compiler diagnostics should allow TextureSample nodes with inline texture slots");
}

void RunMaterialParserDiagnosticsCarrySourceContextTest() {
    const kb::assets::AssetId assetId{ 0x4D41544449414701ULL };
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "kbmat_diagnostics_context.kbmat";
    {
        std::ofstream output{ path, std::ios::trunc };
        output
            << "version 1\n"
            << "materialType builtin.pbr\n"
            << "materialTypeVersion 1\n"
            << "baseColor 1 1 1 1\n"
            << "clearcoatFactor 0.25\n"
            << "unknownMaterialField 7\n";
    }

    const RenderMaterialAssetParseResult fileResult = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(path, assetId);
    Require(!fileResult.asset.has_value(), "KBMAT-1004: File parser should fail on unknown material field");
    Require(fileResult.diagnostics.size() == 2U, "KBMAT-1004: File parser should report warning and error diagnostics");
    Require(fileResult.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField &&
            fileResult.diagnostics[0].severity == RenderMaterialAssetParseDiagnosticSeverity::Warning,
        "KBMAT-1004: File parser warning should have typed diagnostic severity/code");
    Require(fileResult.diagnostics[0].assetId == assetId && fileResult.diagnostics[0].path == path && fileResult.diagnostics[0].line == 5U,
        "KBMAT-1004: File parser warning should carry asset id, path and source line");
    Require(fileResult.diagnostics[1].code == RenderMaterialAssetParseDiagnosticCode::UnknownField &&
            fileResult.diagnostics[1].severity == RenderMaterialAssetParseDiagnosticSeverity::Error,
        "KBMAT-1004: File parser error should have typed diagnostic severity/code");
    Require(fileResult.diagnostics[1].assetId == assetId && fileResult.diagnostics[1].path == path && fileResult.diagnostics[1].line == 6U,
        "KBMAT-1004: File parser error should carry asset id, path and source line");
    Require(fileResult.ErrorMessage().find("asset " + std::to_string(assetId.value)) != std::string::npos &&
            fileResult.ErrorMessage().find(path.generic_string()) != std::string::npos &&
            fileResult.ErrorMessage().find("line 6") != std::string::npos,
        "KBMAT-1004: Error message should include asset, path and line context");

    std::istringstream stream{
        "version 1\n"
        "materialType builtin.pbr\n"
        "materialTypeVersion 1\n"
        "baseColor 1 1 1 1\n"
        "normalTexture textures/normal.kbtex\n"
    };
    const RenderMaterialAssetParseResult streamResult = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(
        stream,
        RenderMaterialAssetParseSourceContext{
            .assetId = assetId,
            .path = path,
        });
    Require(streamResult.asset.has_value(), "KBMAT-1004: Stream parser with context should keep valid material parseable");
    Require(!streamResult.diagnostics.empty(), "KBMAT-1004: Stream parser should report texture color-space warning");
    Require(streamResult.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::TextureColorSpaceExpectation, "KBMAT-1004: Stream diagnostic should keep typed code");
    Require(streamResult.diagnostics[0].assetId == assetId && streamResult.diagnostics[0].path == path && streamResult.diagnostics[0].line == 5U,
        "KBMAT-1004: Stream diagnostic should carry explicit asset id, path and source line");

    std::error_code error;
    std::filesystem::remove(path, error);
}

void RunMaterialGraphSurfaceContractDefaultsTest() {
    const MaterialSurface s;
    Require(NearlyEqual(s.baseColor[0], 1.0F) && NearlyEqual(s.baseColor[1], 1.0F) && NearlyEqual(s.baseColor[2], 1.0F) && NearlyEqual(s.baseColor[3], 1.0F),
        "KBMAT-MAT01: MaterialSurface default baseColor must be white (1,1,1,1)");
    Require(NearlyEqual(s.roughness, 1.0F), "KBMAT-MAT01: MaterialSurface default roughness must be 1.0");
    Require(NearlyEqual(s.metallic, 0.0F), "KBMAT-MAT01: MaterialSurface default metallic must be 0.0");
    Require(NearlyEqual(s.occlusion, 1.0F), "KBMAT-MAT01: MaterialSurface default occlusion must be 1.0");
    Require(NearlyEqual(s.emissive[0], 0.0F) && NearlyEqual(s.emissive[1], 0.0F) && NearlyEqual(s.emissive[2], 0.0F),
        "KBMAT-MAT01: MaterialSurface default emissive must be black");
    Require(NearlyEqual(s.alpha, 1.0F), "KBMAT-MAT01: MaterialSurface default alpha must be 1.0");
    Require(NearlyEqual(s.alphaClipThreshold, 0.5F), "KBMAT-MAT01: MaterialSurface default alphaClipThreshold must be 0.5");
    Require(NearlyEqual(s.normal[0], 0.0F) && NearlyEqual(s.normal[1], 0.0F) && NearlyEqual(s.normal[2], 1.0F),
        "KBMAT-MAT01: MaterialSurface default normal must be up (0,0,1)");
    Require(NearlyEqual(s.specular, 0.5F), "KBMAT-MAT01: MaterialSurface default specular must be 0.5");
    Require(NearlyEqual(s.tangentOutput[0], 1.0F) && NearlyEqual(s.tangentOutput[1], 0.0F) && NearlyEqual(s.tangentOutput[2], 0.0F),
        "KBMAT-MAT50: MaterialSurface default tangentOutput must be +X");
    Require(s.alphaMode == MaterialSurfaceAlphaMode::Opaque, "KBMAT-MAT01: MaterialSurface default alphaMode must be Opaque");
    Require(s.blendMode == MaterialSurfaceBlendMode::Opaque, "KBMAT-MAT01: MaterialSurface default blendMode must be Opaque");
    Require(s.renderQueue == MaterialSurfaceRenderQueue::Opaque, "KBMAT-MAT01: MaterialSurface default renderQueue must be Opaque");
    Require(!s.twoSided, "KBMAT-MAT01: MaterialSurface default twoSided must be false");
    Require(s.depthWrite, "KBMAT-MAT01: MaterialSurface default depthWrite must be true");
    Require(s.castShadow, "KBMAT-MAT01: MaterialSurface default castShadow must be true");

    const MaterialSurface fromFunc = DefaultMaterialSurface();
    Require(NearlyEqual(fromFunc.baseColor[0], s.baseColor[0]) && NearlyEqual(fromFunc.roughness, s.roughness),
        "KBMAT-MAT01: DefaultMaterialSurface() must return values matching struct member initializers");
}

void RunMaterialGraphContextContractDefaultsTest() {
    const MaterialGraphContext ctx;
    Require(NearlyEqual(ctx.uv0[0], 0.0F) && NearlyEqual(ctx.uv0[1], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default uv0 must be (0,0)");
    Require(NearlyEqual(ctx.uv1[0], 0.0F) && NearlyEqual(ctx.uv1[1], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default uv1 must be (0,0)");
    Require(NearlyEqual(ctx.normal[0], 0.0F) && NearlyEqual(ctx.normal[1], 0.0F) && NearlyEqual(ctx.normal[2], 1.0F),
        "KBMAT-MAT01: MaterialGraphContext default normal must be up (0,0,1)");
    Require(NearlyEqual(ctx.tangent[0], 1.0F) && NearlyEqual(ctx.tangent[1], 0.0F) && NearlyEqual(ctx.tangent[2], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default tangent must be (1,0,0)");
    Require(NearlyEqual(ctx.bitangent[0], 0.0F) && NearlyEqual(ctx.bitangent[1], 1.0F) && NearlyEqual(ctx.bitangent[2], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default bitangent must be (0,1,0)");
    Require(NearlyEqual(ctx.worldPos[0], 0.0F) && NearlyEqual(ctx.worldPos[1], 0.0F) && NearlyEqual(ctx.worldPos[2], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default worldPos must be origin");
    Require(NearlyEqual(ctx.viewDir[0], 0.0F) && NearlyEqual(ctx.viewDir[1], 0.0F) && NearlyEqual(ctx.viewDir[2], 1.0F),
        "KBMAT-MAT01: MaterialGraphContext default viewDir must be (0,0,1)");
    Require(NearlyEqual(ctx.vertexColor[0], 1.0F) && NearlyEqual(ctx.vertexColor[1], 1.0F) && NearlyEqual(ctx.vertexColor[2], 1.0F) && NearlyEqual(ctx.vertexColor[3], 1.0F),
        "KBMAT-MAT01: MaterialGraphContext default vertexColor must be white");
    Require(NearlyEqual(ctx.time, 0.0F), "KBMAT-MAT01: MaterialGraphContext default time must be 0.0");
    Require(NearlyEqual(ctx.deltaTime, 0.0F), "KBMAT-MAT30: MaterialGraphContext default deltaTime must be 0.0");
    Require(NearlyEqual(ctx.dynamicParameter[0], 0.0F) && NearlyEqual(ctx.dynamicParameter[1], 0.0F) &&
            NearlyEqual(ctx.dynamicParameter[2], 0.0F) && NearlyEqual(ctx.dynamicParameter[3], 0.0F),
        "KBMAT-MAT30: MaterialGraphContext default dynamicParameter must be zero");
    Require(NearlyEqual(ctx.screenPosition[0], 0.0F) && NearlyEqual(ctx.screenPosition[1], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default screenPosition must be (0,0)");
    Require(NearlyEqual(ctx.localPosition[0], 0.0F) && NearlyEqual(ctx.localPosition[1], 0.0F) && NearlyEqual(ctx.localPosition[2], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default localPosition must be origin");
    Require(NearlyEqual(ctx.objectPosition[0], 0.0F) && NearlyEqual(ctx.objectPosition[1], 0.0F) && NearlyEqual(ctx.objectPosition[2], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default objectPosition must be origin");
    Require(NearlyEqual(ctx.perInstanceRandom, 0.0F), "KBMAT-MAT01: MaterialGraphContext default perInstanceRandom must be 0.0");
    Require(NearlyEqual(ctx.perInstanceFadeAmount, 1.0F), "KBMAT-MAT01: MaterialGraphContext default perInstanceFadeAmount must be 1.0");
    Require(NearlyEqual(ctx.perInstanceCustomData, 0.0F), "KBMAT-MAT01: MaterialGraphContext default perInstanceCustomData must be 0.0");
    Require(NearlyEqual(ctx.objectRadius, 0.0F), "KBMAT-MAT01: MaterialGraphContext default objectRadius must be 0.0");
    Require(NearlyEqual(ctx.objectBounds[0], 0.0F) && NearlyEqual(ctx.objectBounds[1], 0.0F) &&
            NearlyEqual(ctx.objectBounds[2], 0.0F) && NearlyEqual(ctx.objectBounds[3], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default objectBounds must be zero");
    Require(NearlyEqual(ctx.objectOrientation[0], 0.0F) && NearlyEqual(ctx.objectOrientation[1], 0.0F) && NearlyEqual(ctx.objectOrientation[2], 1.0F),
        "KBMAT-MAT01: MaterialGraphContext default objectOrientation must be +Z");
    Require(NearlyEqual(ctx.preSkinnedPosition[0], 0.0F) && NearlyEqual(ctx.preSkinnedPosition[1], 0.0F) && NearlyEqual(ctx.preSkinnedPosition[2], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default preSkinnedPosition must be origin");
    Require(NearlyEqual(ctx.preSkinnedNormal[0], 0.0F) && NearlyEqual(ctx.preSkinnedNormal[1], 0.0F) && NearlyEqual(ctx.preSkinnedNormal[2], 1.0F),
        "KBMAT-MAT01: MaterialGraphContext default preSkinnedNormal must be +Z");
    Require(NearlyEqual(ctx.cameraPosition[0], 0.0F) && NearlyEqual(ctx.cameraPosition[1], 0.0F) && NearlyEqual(ctx.cameraPosition[2], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default cameraPosition must be origin");
    Require(NearlyEqual(ctx.lightVector[0], 0.0F) && NearlyEqual(ctx.lightVector[1], 1.0F) && NearlyEqual(ctx.lightVector[2], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default lightVector must be +Y");
    Require(NearlyEqual(ctx.viewSize[0], 0.0F) && NearlyEqual(ctx.viewSize[1], 0.0F),
        "KBMAT-MAT01: MaterialGraphContext default viewSize must be (0,0)");
    Require(NearlyEqual(ctx.twoSidedSign, 1.0F), "KBMAT-MAT01: MaterialGraphContext default twoSidedSign must be front-facing (+1)");
    Require(NearlyEqual(ctx.fragmentDepth, 0.0F), "KBMAT-MAT01: MaterialGraphContext default fragmentDepth must be 0.0");

    const MaterialGraphContext fromFunc = DefaultMaterialGraphContext();
    Require(NearlyEqual(fromFunc.uv0[0], ctx.uv0[0]) && NearlyEqual(fromFunc.normal[2], ctx.normal[2]) &&
            NearlyEqual(fromFunc.objectOrientation[2], ctx.objectOrientation[2]) &&
            NearlyEqual(fromFunc.perInstanceFadeAmount, ctx.perInstanceFadeAmount) &&
            NearlyEqual(fromFunc.deltaTime, ctx.deltaTime) &&
            NearlyEqual(fromFunc.dynamicParameter[3], ctx.dynamicParameter[3]) &&
            NearlyEqual(fromFunc.twoSidedSign, ctx.twoSidedSign),
        "KBMAT-MAT01: DefaultMaterialGraphContext() must return values matching struct member initializers");
}

void RunMaterialGraphAlphaClipThresholdPinTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.3" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 120,
        .positionY = 180,
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alphaClipThreshold"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphIrBuildResult ir = BuildRenderMaterialGraphIr(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0401U });
    Require(ir.Succeeded(), "KBMAT-MAT01: Graph with alphaClipThreshold pin should build valid IR");

    const RenderMaterialGraphIrNode* outputIr = nullptr;
    for (const RenderMaterialGraphIrNode& node : ir.ir.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            outputIr = &node;
            break;
        }
    }
    Require(outputIr != nullptr, "KBMAT-MAT01: IR must contain a MaterialOutput node");
    bool hasAlphaClipThresholdPin = false;
    for (const RenderMaterialGraphIrPin& pin : outputIr->inputs) {
        if (pin.name == "alphaClipThreshold") {
            hasAlphaClipThresholdPin = true;
            Require(pin.type == RenderMaterialGraphPinType::Float, "KBMAT-MAT01: alphaClipThreshold IR pin must have Float type");
            Require(pin.stablePinId == RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "alphaClipThreshold", false),
                "KBMAT-MAT01: alphaClipThreshold must have a non-zero stable pin id");
            break;
        }
    }
    Require(hasAlphaClipThresholdPin, "KBMAT-MAT01: MaterialOutput IR must include alphaClipThreshold input pin");

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
        graph, RenderMaterialGraphBuildContext{ .assetId = 0x0401U });
    Require(compiled.Succeeded(), "KBMAT-MAT01: Graph with alphaClipThreshold should compile to shader source");
    Require(compiled.shader.source.find("material.alphaClipThreshold = 0.3;") != std::string::npos,
        "KBMAT-MAT01: Compiled shader should emit authored alphaClipThreshold value");
}

void RunMaterialGraphOutputPinTypeMismatchDiagnosticTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = 120,
        .positionY = 80,
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const std::vector<RenderMaterialGraphDiagnostic> diagnostics = ValidateRenderMaterialGraphDocument(graph);
    const RenderMaterialGraphDiagnostic* mismatch = FindGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::TypeMismatch);
    Require(mismatch != nullptr, "KBMAT-MAT01: Connecting Float to baseColor (Color) must produce TypeMismatch diagnostic");
    Require(mismatch->pin == "baseColor", "KBMAT-MAT01: TypeMismatch diagnostic must identify the baseColor pin");
}

void RunMaterialGraphTextureSamplerLimitTest() {
    // Builds a graph with `textureCount` reachable TextureSample nodes chained through Add nodes into baseColor.
    const auto buildGraph = [](std::uint32_t textureCount) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        RenderMaterialGraphNodeKind accKind = RenderMaterialGraphNodeKind::TextureSample;
        std::uint32_t accId = 0U;
        std::string accPin = "color";
        for (std::uint32_t i = 0U; i < textureCount; ++i) {
            const std::uint32_t texId = 100U + i;
            graph.nodes.push_back(RenderMaterialGraphNode{
                .id = texId,
                .kind = RenderMaterialGraphNodeKind::TextureSample,
                .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "tex" + std::to_string(i), .textureRole = "baseColor" },
            });
            if (i == 0U) {
                accId = texId;
            } else {
                const std::uint32_t addId = 500U + i;
                graph.nodes.push_back(RenderMaterialGraphNode{ .id = addId, .kind = RenderMaterialGraphNodeKind::Add });
                graph.links.push_back(MakeGraphLink(accKind, accId, accPin, RenderMaterialGraphNodeKind::Add, addId, "a"));
                graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, texId, "color", RenderMaterialGraphNodeKind::Add, addId, "b"));
                accKind = RenderMaterialGraphNodeKind::Add;
                accId = addId;
                accPin = "value";
            }
        }
        graph.links.push_back(MakeGraphLink(accKind, accId, accPin, RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };

    const std::uint32_t available = kRenderMaterialGraphMaxTextureSamplers - kRenderMaterialGraphTextureBaseSlot;

    const RenderMaterialGraphCompileResult atLimit = CompileRenderMaterialGraphToShaderSource(
        buildGraph(available), RenderMaterialGraphBuildContext{ .assetId = 0x0780U });
    Require(atLimit.Succeeded(), "KBMAT-MAT78: A graph at the sampler ceiling must still compile");
    Require(atLimit.shader.reflection.textures.size() == available, "KBMAT-MAT78: All textures at the limit must be reflected");
    Require(atLimit.shader.reflection.textures.back().slot == kRenderMaterialGraphMaxTextureSamplers - 1U,
        "KBMAT-MAT78: The last graph texture must occupy the final available stage");

    const RenderMaterialGraphCompileResult overLimit = CompileRenderMaterialGraphToShaderSource(
        buildGraph(available + 1U), RenderMaterialGraphBuildContext{ .assetId = 0x0781U });
    Require(!overLimit.Succeeded(), "KBMAT-MAT78: Exceeding the sampler ceiling must fail compilation");
    const RenderMaterialGraphDiagnostic* limitDiag = FindGraphDiagnostic(overLimit.diagnostics, RenderMaterialGraphDiagnosticKind::TextureSamplerLimitExceeded);
    Require(limitDiag != nullptr && limitDiag->severity == RenderMaterialGraphDiagnosticSeverity::Error,
        "KBMAT-MAT78: Exceeding the sampler ceiling must emit a TextureSamplerLimitExceeded error diagnostic");
}

void RunMaterialOutputPinsCodegenTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.8" } });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "specular"));

    const RenderMaterialGraphCompileResult result = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0350U });
    Require(result.Succeeded(), "KBMAT-MAT35: specular output pin graph must compile");
    const std::string& src = result.shader.source;
    // The surface contract declares the specular field (it feeds the forward BRDF's F0 directly).
    Require(src.find("float specular;") != std::string::npos,
        "KBMAT-MAT35: the MaterialSurface struct must declare the specular output field");
    // A connected pin generates code from its input (not the default).
    Require(src.find("material.specular = ") != std::string::npos && src.find("material.specular = 0.5;") == std::string::npos,
        "KBMAT-MAT35: a connected specular pin must generate code from its input, not the default");
}

void RunMaterialCustomOutputsSchemaAndCodegenTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantVector, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 1 0" } });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 2U, "xyz", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "tangentOutput"));

    const RenderMaterialGraphIrBuildResult ir = BuildRenderMaterialGraphIr(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0500U });
    Require(ir.Succeeded(), "KBMAT-MAT50: custom output graph must build IR");
    const RenderMaterialGraphIrNode* output = nullptr;
    for (const RenderMaterialGraphIrNode& node : ir.ir.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            output = &node;
            break;
        }
    }
    Require(output != nullptr, "KBMAT-MAT50: custom output IR must contain MaterialOutput");

    const auto found = std::ranges::find_if(output->inputs, [](const RenderMaterialGraphIrPin& pin) {
        return pin.name == "tangentOutput";
    });
    Require(found != output->inputs.end(), "KBMAT-MAT50: MaterialOutput must expose the tangentOutput pin in IR");
    Require(found->type == RenderMaterialGraphPinType::Float3, "KBMAT-MAT50: MaterialOutput tangentOutput pin has the wrong type");
    Require(found->stablePinId == RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "tangentOutput", false),
        "KBMAT-MAT50: MaterialOutput tangentOutput pin must have a stable id");

    const RenderMaterialGraphCompileResult direct = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0500U });
    Require(direct.Succeeded(), "KBMAT-MAT50: custom output graph must compile");
    Require(direct.shader.reflection.hasTangentOutput, "KBMAT-MAT50: a connected tangentOutput link must set the reflection flag");
    const std::string& src = direct.shader.source;
    Require(src.find("vec3 tangentOutput;") != std::string::npos,
        "KBMAT-MAT50: MaterialSurface shader struct must declare the tangentOutput field");
    Require(src.find("material.tangentOutput = ") != std::string::npos,
        "KBMAT-MAT50: MaterialOutput must assign the tangentOutput field");

    const std::string wrapper = BuildGraphFragmentWrapperSource(direct.shader, "BaseOpaque");
    Require(wrapper.find("surface.tangentOutput") != std::string::npos,
        "KBMAT-MAT50: tangentOutput must be consumed by the forward fragment wrapper");

    RenderMaterialGraphDocument attrsGraph = MakeDefaultRenderMaterialGraphDocument();
    attrsGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantVector, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 1 0" } });
    attrsGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantScalar, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.6" } });
    attrsGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::MakeMaterialAttributes });
    attrsGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::SetMaterialAttributes });
    attrsGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 6U, .kind = RenderMaterialGraphNodeKind::GetMaterialAttributes });
    attrsGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 7U, .kind = RenderMaterialGraphNodeKind::BreakMaterialAttributes });
    attrsGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 2U, "xyz", RenderMaterialGraphNodeKind::MakeMaterialAttributes, 4U, "tangentOutput"));
    attrsGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::MakeMaterialAttributes, 4U, "specular"));
    attrsGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::MakeMaterialAttributes, 4U, "attributes", RenderMaterialGraphNodeKind::SetMaterialAttributes, 5U, "attributes"));
    attrsGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 2U, "xyz", RenderMaterialGraphNodeKind::SetMaterialAttributes, 5U, "tangentOutput"));
    attrsGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value", RenderMaterialGraphNodeKind::SetMaterialAttributes, 5U, "specular"));
    attrsGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::SetMaterialAttributes, 5U, "attributesOut", RenderMaterialGraphNodeKind::GetMaterialAttributes, 6U, "attributes"));
    attrsGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::GetMaterialAttributes, 6U, "specular", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "specular"));
    attrsGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::SetMaterialAttributes, 5U, "attributesOut", RenderMaterialGraphNodeKind::BreakMaterialAttributes, 7U, "attributes"));
    attrsGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::BreakMaterialAttributes, 7U, "tangentOutput", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "tangentOutput"));

    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::MakeMaterialAttributes, "tangentOutput", false) == RenderMaterialGraphPinType::Float3 &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::BreakMaterialAttributes, "tangentOutput", true) == RenderMaterialGraphPinType::Float3 &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::SetMaterialAttributes, "specular", false) == RenderMaterialGraphPinType::Float,
        "KBMAT-MAT50: MaterialAttributes nodes must expose typed custom output channels");
    const RenderMaterialGraphCompileResult attrs = CompileRenderMaterialGraphToShaderSource(attrsGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0501U });
    Require(attrs.Succeeded(), "KBMAT-MAT50: custom outputs must survive Make/Set/Get/Break MaterialAttributes");
    Require(attrs.shader.source.find("attrs4.tangentOutput") != std::string::npos &&
            attrs.shader.source.find("attrsSet5.specular") != std::string::npos,
        "KBMAT-MAT50: MaterialAttributes codegen must carry custom output fields");
}

void RunMaterialDomainGatingTest() {
    RenderMaterialGraphDocument surfaceGraph = MakeDefaultRenderMaterialGraphDocument();
    surfaceGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 1 1 1" } });
    surfaceGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult surfaceResult = CompileRenderMaterialGraphToShaderSource(surfaceGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0340U });
    Require(surfaceResult.Succeeded(), "KBMAT-MAT34: Surface domain graph must compile");
    Require(FindGraphDiagnostic(surfaceResult.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedMaterialDomain) == nullptr,
        "KBMAT-MAT34: the production Surface domain must not emit an unsupported-domain diagnostic");

    // A declared-but-unimplemented domain fails instead of being silently compiled as Surface.
    RenderMaterialGraphDocument volumeGraph = surfaceGraph;
    volumeGraph.materialDomain = "volume";
    const RenderMaterialGraphCompileResult volumeResult = CompileRenderMaterialGraphToShaderSource(volumeGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0341U });
    Require(!volumeResult.Succeeded(), "KBMAT-MAT34: a non-production domain must fail instead of compiling as Surface");
    Require(volumeResult.shader.source.empty(), "KBMAT-MAT34: a non-production domain must not produce a Surface shader");
    const RenderMaterialGraphDiagnostic* domainDiag = FindGraphDiagnostic(volumeResult.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedMaterialDomain);
    Require(domainDiag != nullptr && domainDiag->severity == RenderMaterialGraphDiagnosticSeverity::Error,
        "KBMAT-MAT34: an unimplemented domain must emit an error diagnostic without a Surface fallback");

    Require(IsRenderMaterialDomainProduction(RenderMaterialDomain::Surface), "KBMAT-MAT34: Surface domain must be production");
    Require(!IsRenderMaterialDomainProduction(RenderMaterialDomain::DeferredDecal) &&
            !IsRenderMaterialDomainProduction(RenderMaterialDomain::LightFunction) &&
            !IsRenderMaterialDomainProduction(RenderMaterialDomain::Volume) &&
            !IsRenderMaterialDomainProduction(RenderMaterialDomain::PostProcess) &&
            !IsRenderMaterialDomainProduction(RenderMaterialDomain::UserInterface),
            "KBMAT-MAT34: only Surface is production; the other declared domains must be non-production");
}

void RunMaterialShadingModelGatingTest() {
    RenderMaterialGraphDocument litGraph = MakeDefaultRenderMaterialGraphDocument();
    litGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 1 1 1" } });
    litGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    // DefaultLit (the default) is production: no diagnostic, reflection resolves to DefaultLit.
    const RenderMaterialGraphCompileResult litResult = CompileRenderMaterialGraphToShaderSource(litGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0370U });
    Require(litResult.Succeeded(), "KBMAT-MAT37: DefaultLit graph must compile");
    Require(FindGraphDiagnostic(litResult.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedShadingModel) == nullptr,
        "KBMAT-MAT37: the production DefaultLit model must not emit an unsupported-shading-model diagnostic");
    Require(litResult.shader.reflection.shadingModel == RenderMaterialShadingModel::DefaultLit,
        "KBMAT-MAT37: the default shading model must resolve to DefaultLit");

    // Unlit is production and resolves to a distinct model — the program key differs from DefaultLit.
    RenderMaterialGraphDocument unlitGraph = litGraph;
    unlitGraph.shadingModel = "unlit";
    const RenderMaterialGraphCompileResult unlitResult = CompileRenderMaterialGraphToShaderSource(unlitGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0371U });
    Require(unlitResult.Succeeded(), "KBMAT-MAT37: Unlit graph must compile");
    Require(FindGraphDiagnostic(unlitResult.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedShadingModel) == nullptr,
        "KBMAT-MAT37: the production Unlit model must not emit a diagnostic");
    Require(unlitResult.shader.reflection.shadingModel == RenderMaterialShadingModel::Unlit,
        "KBMAT-MAT37: an Unlit graph must resolve to the Unlit model");
    Require(ComputeRenderMaterialGraphReflectionHash(unlitResult.shader.reflection) != ComputeRenderMaterialGraphReflectionHash(litResult.shader.reflection),
        "KBMAT-MAT37: Unlit and DefaultLit must hash to different program identities");

    // Subsurface/ClearCoat/SingleLayerWater/ThinTranslucent are still selectable Shading Model values
    // (kept for forward compatibility / project settings that already reference them), but since the
    // Material Output pins that used to feed their special-case terms (subsurfaceColor, clearCoatNormal,
    // clearCoat, clearCoatRoughness, surfaceThickness, thinTranslucentOutput, singleLayerWaterOutput)
    // were removed, these models now compile to the exact same DefaultLit forward-lighting wrapper --
    // no per-model branch, no dead pin references. Verify that explicitly instead of asserting on
    // branch markers that no longer exist.
    for (const RenderMaterialShadingModel model : { RenderMaterialShadingModel::Subsurface,
             RenderMaterialShadingModel::ClearCoat,
             RenderMaterialShadingModel::SingleLayerWater,
             RenderMaterialShadingModel::ThinTranslucent }) {
        RenderMaterialGraphDocument modelGraph = litGraph;
        modelGraph.shadingModel = std::string{ RenderMaterialShadingModelName(model) };
        const RenderMaterialGraphCompileResult modelResult = CompileRenderMaterialGraphToShaderSource(modelGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0372U });
        Require(modelResult.Succeeded(), "KBMAT-MAT37: a legacy shading model token must still compile");
        Require(modelResult.shader.reflection.shadingModel == model,
            "KBMAT-MAT37: legacy shading model reflection must still record the requested model");
        Require(FindGraphDiagnostic(modelResult.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedShadingModel) == nullptr,
            "KBMAT-MAT37: legacy shading model tokens must not emit an unsupported diagnostic");
        const std::string wrapper = BuildGraphFragmentWrapperSource(modelResult.shader, "BaseOpaque");
        Require(wrapper.find("KbEvaluateForwardLighting(") != std::string::npos,
            "KBMAT-MAT37: legacy shading models fall back to the standard forward lighting call");
        Require(wrapper.find("subsurfaceColor") == std::string::npos && wrapper.find("clearCoatNormal") == std::string::npos &&
                wrapper.find("singleLayerWaterOutput") == std::string::npos && wrapper.find("thinTranslucentOutput") == std::string::npos,
            "KBMAT-MAT37: legacy shading models must not emit removed per-model surface fields");
    }

    RenderMaterialGraphDocument hairGraph = litGraph;
    hairGraph.shadingModel = "hair";
    const RenderMaterialGraphCompileResult hairResult = CompileRenderMaterialGraphToShaderSource(hairGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0376U });
    Require(!hairResult.Succeeded(), "KBMAT-MAT37: a declared shading model without a production branch must fail instead of falling back");
    const RenderMaterialGraphDiagnostic* shadingDiag = FindGraphDiagnostic(hairResult.diagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedShadingModel);
    Require(shadingDiag != nullptr && shadingDiag->severity == RenderMaterialGraphDiagnosticSeverity::Error,
        "KBMAT-MAT37: an unimplemented shading model must emit an error diagnostic without a DefaultLit fallback");

    Require(IsRenderMaterialShadingModelProduction(RenderMaterialShadingModel::Unlit) &&
            IsRenderMaterialShadingModelProduction(RenderMaterialShadingModel::DefaultLit) &&
            IsRenderMaterialShadingModelProduction(RenderMaterialShadingModel::Subsurface) &&
            IsRenderMaterialShadingModelProduction(RenderMaterialShadingModel::ClearCoat) &&
            IsRenderMaterialShadingModelProduction(RenderMaterialShadingModel::SingleLayerWater) &&
            IsRenderMaterialShadingModelProduction(RenderMaterialShadingModel::ThinTranslucent),
            "KBMAT-MAT37: Unlit, DefaultLit and implemented advanced models must be production shading models");
    Require(!IsRenderMaterialShadingModelProduction(RenderMaterialShadingModel::Cloth) &&
            !IsRenderMaterialShadingModelProduction(RenderMaterialShadingModel::Hair) &&
            !IsRenderMaterialShadingModelProduction(RenderMaterialShadingModel::Eye),
            "KBMAT-MAT37: declared shading models without runtime branches must remain non-production");
}

void RunMaterialGraphBlendModeTest() {
    RenderMaterialGraphDocument opaqueGraph = MakeDefaultRenderMaterialGraphDocument();
    opaqueGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 0.5" } });
    opaqueGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult opaqueResult = CompileRenderMaterialGraphToShaderSource(opaqueGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0380U });
    Require(opaqueResult.Succeeded() && opaqueResult.shader.reflection.blendMode == RenderMaterialGraphBlendMode::Opaque,
        "KBMAT-MAT38: the default blend mode must resolve to Opaque");

    // Masked resolves to Masked and emits a clip in the base-pass wrapper.
    RenderMaterialGraphDocument maskedGraph = opaqueGraph;
    maskedGraph.blendMode = "masked";
    const RenderMaterialGraphCompileResult maskedResult = CompileRenderMaterialGraphToShaderSource(maskedGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0381U });
    Require(maskedResult.Succeeded() && maskedResult.shader.reflection.blendMode == RenderMaterialGraphBlendMode::Masked,
        "KBMAT-MAT38: a masked graph must resolve to the Masked blend mode");
    Require(ComputeRenderMaterialGraphReflectionHash(maskedResult.shader.reflection) != ComputeRenderMaterialGraphReflectionHash(opaqueResult.shader.reflection),
        "KBMAT-MAT38: Masked and Opaque must hash to different program identities");

    // Each transparent mode resolves to its own value and is flagged transparent.
    for (const auto& [token, mode] : std::initializer_list<std::pair<const char*, RenderMaterialGraphBlendMode>>{
             { "translucent", RenderMaterialGraphBlendMode::Translucent },
             { "additive", RenderMaterialGraphBlendMode::Additive },
             { "modulate", RenderMaterialGraphBlendMode::Modulate },
             { "alphaComposite", RenderMaterialGraphBlendMode::AlphaComposite },
             { "alphaHoldout", RenderMaterialGraphBlendMode::AlphaHoldout } }) {
        RenderMaterialGraphDocument g = opaqueGraph;
        g.blendMode = token;
        const RenderMaterialGraphCompileResult r = CompileRenderMaterialGraphToShaderSource(g, RenderMaterialGraphBuildContext{ .assetId = 0x0382U });
        Require(r.Succeeded() && r.shader.reflection.blendMode == mode, "KBMAT-MAT38: a transparent blend mode must resolve to its declared value");
        Require(IsRenderMaterialGraphBlendModeTransparent(mode), "KBMAT-MAT38: translucent/additive/modulate/composite/holdout must be transparent modes");
    }
    Require(!IsRenderMaterialGraphBlendModeTransparent(RenderMaterialGraphBlendMode::Opaque) &&
            !IsRenderMaterialGraphBlendModeTransparent(RenderMaterialGraphBlendMode::Masked),
            "KBMAT-MAT38: Opaque and Masked must not be transparent modes");

    // Parser round-trip of the graphBlendMode field.
    std::istringstream input{
        "version 1\n"
        "materialType builtin.pbr\n"
        "graphVersion 1\n"
        "graphBlendMode additive\n"
        "graphNode 1 MaterialOutput 0 0\n"
    };
    const std::optional<RenderMaterialAssetData> parsed = RenderMaterialAssetParser::Parse(input);
    Require(parsed.has_value() && parsed->graph.blendMode == "additive", "KBMAT-MAT38: graphBlendMode must parse into the graph document");
}

void RunMaterialGraphBlendSceneStateTest() {
    // #25d: the graph blend mode resolves into the program binding's scene render state (alpha mode +
    // translucency blend), which drives the transparent pass + blend equation for graph materials.
    struct Case {
        const char* token;
        RenderMaterialAlphaMode alphaMode;
        RenderMaterialTranslucencyBlend translucencyBlend;
    };
    const std::array<Case, 7U> cases{ {
        { "opaque", RenderMaterialAlphaMode::Opaque, RenderMaterialTranslucencyBlend::Alpha },
        { "masked", RenderMaterialAlphaMode::Mask, RenderMaterialTranslucencyBlend::Alpha },
        { "translucent", RenderMaterialAlphaMode::Blend, RenderMaterialTranslucencyBlend::Alpha },
        { "additive", RenderMaterialAlphaMode::Blend, RenderMaterialTranslucencyBlend::Additive },
        { "modulate", RenderMaterialAlphaMode::Blend, RenderMaterialTranslucencyBlend::Modulate },
        { "alphaComposite", RenderMaterialAlphaMode::Blend, RenderMaterialTranslucencyBlend::PreMultipliedAlpha },
        { "alphaHoldout", RenderMaterialAlphaMode::Blend, RenderMaterialTranslucencyBlend::AlphaHoldout },
    } };

    for (const Case& testCase : cases) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.blendMode = testCase.token;
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 0.5" } });
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

        const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0385U });
        Require(compiled.Succeeded(), "KBMAT-MAT38-25d: blend-mode graph must compile");
        const RenderMaterialGraphProgramBindingResult bindingResult =
            BuildRenderMaterialGraphProgramBinding(0x1234U, 1U, compiled.shader, std::span<const RenderMaterialGraphParameterValue>{});
        Require(bindingResult.binding.alphaMode == testCase.alphaMode,
            "KBMAT-MAT38-25d: graph blend mode must resolve to the matching scene alpha mode");
        if (testCase.alphaMode == RenderMaterialAlphaMode::Blend) {
            Require(bindingResult.binding.translucencyBlend == testCase.translucencyBlend,
                "KBMAT-MAT38-25d: a translucent graph blend mode must resolve to the matching bgfx blend equation");
        }
    }
}

void RunMaterialStaticPermutationTest() {
    // A StaticBoolParameter selects which constant a StaticSwitch routes into MaterialOutput.baseColor.
    const auto buildSwitchGraph = [](const char* boolHint) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::StaticBoolParameter, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = boolHint } });
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" } });
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 1 1" } });
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::StaticSwitch });
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticBoolParameter, 2U, "value", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "value"));
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "true"));
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "false"));
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticSwitch, 5U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };

    const RenderMaterialGraphCompileResult trueResult = CompileRenderMaterialGraphToShaderSource(buildSwitchGraph("true"), RenderMaterialGraphBuildContext{ .assetId = 0x0390U });
    const RenderMaterialGraphCompileResult falseResult = CompileRenderMaterialGraphToShaderSource(buildSwitchGraph("false"), RenderMaterialGraphBuildContext{ .assetId = 0x0391U });
    Require(trueResult.Succeeded() && falseResult.Succeeded(), "KBMAT-MAT39: static switch graphs must compile");
    Require(trueResult.shader.sourceHash != falseResult.shader.sourceHash,
        "KBMAT-MAT39: flipping a static switch must change the shader source / variant key");
    // Dead-branch elimination: only the selected constant is present in the source.
    Require(trueResult.shader.source.find("vec4(1.0, 0.0, 0.0, 1.0)") != std::string::npos && trueResult.shader.source.find("vec4(0.0, 0.0, 1.0, 1.0)") == std::string::npos,
        "KBMAT-MAT39: the true branch must compile only the red constant (blue branch eliminated)");
    Require(falseResult.shader.source.find("vec4(0.0, 0.0, 1.0, 1.0)") != std::string::npos && falseResult.shader.source.find("vec4(1.0, 0.0, 0.0, 1.0)") == std::string::npos,
        "KBMAT-MAT39: the false branch must compile only the blue constant (red branch eliminated)");

    // Dynamic invariance: changing a ParameterScalar default does NOT change the source / variant key.
    const auto buildDynamicGraph = [](const char* scalarDefault) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ParameterScalar, .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "metal", .defaultValueHint = scalarDefault } });
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
        return graph;
    };
    const RenderMaterialGraphCompileResult dynamicLow = CompileRenderMaterialGraphToShaderSource(buildDynamicGraph("0.25"), RenderMaterialGraphBuildContext{ .assetId = 0x0392U });
    const RenderMaterialGraphCompileResult dynamicHigh = CompileRenderMaterialGraphToShaderSource(buildDynamicGraph("0.85"), RenderMaterialGraphBuildContext{ .assetId = 0x0392U });
    Require(dynamicLow.Succeeded() && dynamicHigh.Succeeded(), "KBMAT-MAT39: dynamic parameter graphs must compile");
    Require(dynamicLow.shader.sourceHash == dynamicHigh.shader.sourceHash,
        "KBMAT-MAT39: changing a dynamic parameter default must NOT change the variant key (it is a runtime uniform)");

    // StaticComponentMask zeroes unselected channels at compile time (static, contributes to the source).
    RenderMaterialGraphDocument maskGraph = MakeDefaultRenderMaterialGraphDocument();
    maskGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 1 1 1" } });
    maskGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::StaticComponentMask, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "rg" } });
    maskGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::StaticComponentMask, 3U, "input"));
    maskGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticComponentMask, 3U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult maskResult = CompileRenderMaterialGraphToShaderSource(maskGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0393U });
    Require(maskResult.Succeeded(), "KBMAT-MAT39: static component mask graph must compile");
    Require(maskResult.shader.source.find(").x, ") != std::string::npos && maskResult.shader.source.find(", 0.0, 0.0)") != std::string::npos,
        "KBMAT-MAT39: an 'rg' component mask must pass x/y and zero the b/a channels");

    // 2^n explosion warning past the static-switch budget.
    RenderMaterialGraphDocument manySwitches = buildSwitchGraph("true");
    for (std::uint32_t i = 0U; i < 9U; ++i) {
        manySwitches.nodes.push_back(RenderMaterialGraphNode{ .id = 100U + i, .kind = RenderMaterialGraphNodeKind::StaticSwitch });
    }
    const RenderMaterialGraphCompileResult explosionResult = CompileRenderMaterialGraphToShaderSource(manySwitches, RenderMaterialGraphBuildContext{ .assetId = 0x0394U });
    const RenderMaterialGraphDiagnostic* explosionDiag = FindGraphDiagnostic(explosionResult.diagnostics, RenderMaterialGraphDiagnosticKind::StaticPermutationExplosion);
    Require(explosionDiag != nullptr && explosionDiag->severity == RenderMaterialGraphDiagnosticSeverity::Warning,
        "KBMAT-MAT39: exceeding the static-switch budget must emit a permutation-explosion warning");
    Require(FindGraphDiagnostic(trueResult.diagnostics, RenderMaterialGraphDiagnosticKind::StaticPermutationExplosion) == nullptr,
        "KBMAT-MAT39: a single static switch must not warn about permutation explosion");
}

void RunMaterialGraphProgramBindingIdentityKeyTest() {
    const auto buildSwitchGraph = [](const char* boolHint) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::StaticBoolParameter, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = boolHint } });
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 0 0 1" } });
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0 0 1 1" } });
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::StaticSwitch });
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticBoolParameter, 2U, "value", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "value"));
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "true"));
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 4U, "rgba", RenderMaterialGraphNodeKind::StaticSwitch, 5U, "false"));
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::StaticSwitch, 5U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };

    const RenderMaterialGraphCompileResult staticTrue = CompileRenderMaterialGraphToShaderSource(buildSwitchGraph("true"), RenderMaterialGraphBuildContext{ .assetId = 0x0660U });
    const RenderMaterialGraphCompileResult staticFalse = CompileRenderMaterialGraphToShaderSource(buildSwitchGraph("false"), RenderMaterialGraphBuildContext{ .assetId = 0x0661U });
    Require(staticTrue.Succeeded() && staticFalse.Succeeded(), "KBMAT-MAT66: Static variant graphs must compile before binding");
    const RenderMaterialGraphProgramBindingResult trueBinding = BuildRenderMaterialGraphProgramBinding(0x6600U, 1U, staticTrue.shader, std::span<const RenderMaterialGraphParameterValue>{});
    const RenderMaterialGraphProgramBindingResult falseBinding = BuildRenderMaterialGraphProgramBinding(0x6600U, 1U, staticFalse.shader, std::span<const RenderMaterialGraphParameterValue>{});
    Require(trueBinding.binding.variantKey != 0U && trueBinding.binding.pipelineStateKey != 0U,
        "KBMAT-MAT66: Graph program bindings must carry non-zero variant and pipeline-state keys");
    Require(trueBinding.binding.variantKey == RenderMaterialGraphVariantKey(staticTrue.shader) &&
            trueBinding.binding.pipelineStateKey == RenderMaterialGraphPipelineStateKey(staticTrue.shader),
        "KBMAT-MAT66: Binding keys must match the public graph key helpers");
    Require(trueBinding.binding.variantKey != falseBinding.binding.variantKey,
        "KBMAT-MAT66: Static switch permutations must produce distinct variant keys");

    const auto buildDynamicGraph = [](const char* scalarDefault) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ParameterScalar, .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "rough", .defaultValueHint = scalarDefault } });
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
        return graph;
    };
    const RenderMaterialGraphCompileResult dynamicLow = CompileRenderMaterialGraphToShaderSource(buildDynamicGraph("0.1"), RenderMaterialGraphBuildContext{ .assetId = 0x0662U });
    const RenderMaterialGraphCompileResult dynamicHigh = CompileRenderMaterialGraphToShaderSource(buildDynamicGraph("0.9"), RenderMaterialGraphBuildContext{ .assetId = 0x0662U });
    Require(dynamicLow.Succeeded() && dynamicHigh.Succeeded(), "KBMAT-MAT66: Dynamic parameter graphs must compile");
    const RenderMaterialGraphProgramBindingResult dynamicLowBinding = BuildRenderMaterialGraphProgramBinding(0x6601U, 1U, dynamicLow.shader, std::span<const RenderMaterialGraphParameterValue>{});
    const RenderMaterialGraphProgramBindingResult dynamicHighBinding = BuildRenderMaterialGraphProgramBinding(0x6601U, 1U, dynamicHigh.shader, std::span<const RenderMaterialGraphParameterValue>{});
    Require(dynamicLowBinding.binding.graphSourceHash == dynamicHighBinding.binding.graphSourceHash &&
            dynamicLowBinding.binding.variantKey == dynamicHighBinding.binding.variantKey &&
            dynamicLowBinding.binding.pipelineStateKey == dynamicHighBinding.binding.pipelineStateKey,
        "KBMAT-MAT66: Dynamic parameter defaults must not change graph source, variant, or pipeline keys");

    RenderMaterialGraphDocument opaqueGraph = MakeDefaultRenderMaterialGraphDocument();
    opaqueGraph.blendMode = "opaque";
    RenderMaterialGraphDocument additiveGraph = opaqueGraph;
    additiveGraph.blendMode = "additive";
    const RenderMaterialGraphCompileResult opaque = CompileRenderMaterialGraphToShaderSource(opaqueGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0663U });
    const RenderMaterialGraphCompileResult additive = CompileRenderMaterialGraphToShaderSource(additiveGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0664U });
    Require(opaque.Succeeded() && additive.Succeeded(), "KBMAT-MAT66: Blend-mode identity graphs must compile");
    const RenderMaterialGraphProgramBindingResult opaqueBinding = BuildRenderMaterialGraphProgramBinding(0x6602U, 1U, opaque.shader, std::span<const RenderMaterialGraphParameterValue>{});
    const RenderMaterialGraphProgramBindingResult additiveBinding = BuildRenderMaterialGraphProgramBinding(0x6602U, 1U, additive.shader, std::span<const RenderMaterialGraphParameterValue>{});
    Require(opaqueBinding.binding.pipelineStateKey != additiveBinding.binding.pipelineStateKey,
        "KBMAT-MAT66: Blend/render-state changes must produce distinct pipeline-state keys");
}

void RunMaterialVertexDomainOutputCodegenTest() {
    const auto hasPin = [](const std::vector<std::string>& pins, std::string_view pin) {
        return std::find(pins.begin(), pins.end(), pin) != pins.end();
    };

    const std::vector<std::string> outputPins = RenderMaterialGraphNodeInputPinNames(RenderMaterialGraphNodeKind::MaterialOutput);
    Require(hasPin(outputPins, "worldPositionOffset") && hasPin(outputPins, "customizedUv0") && hasPin(outputPins, "displacement"),
        "KBMAT-MAT67: MaterialOutput must expose vertex-domain input pins");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::MaterialOutput, "customizedUv0", false) == RenderMaterialGraphPinType::Float2,
        "KBMAT-MAT67: CustomizedUV0 must be a Float2 MaterialOutput input");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::MaterialOutput, "displacement", false) == RenderMaterialGraphPinType::Float3,
        "KBMAT-MAT67: Displacement must be a Float3 MaterialOutput input");
    Require(RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "customizedUv0", false) != 0U &&
            RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "displacement", false) != 0U,
        "KBMAT-MAT67: vertex-domain output pins must have stable IDs");

    RenderMaterialGraphDocument baseGraph = MakeDefaultRenderMaterialGraphDocument();
    const RenderMaterialGraphCompileResult baseResult = CompileRenderMaterialGraphToShaderSource(baseGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0670U });
    Require(baseResult.Succeeded(), "KBMAT-MAT67: baseline graph must compile");

    RenderMaterialGraphDocument customizedUvGraph = MakeDefaultRenderMaterialGraphDocument();
    customizedUvGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantVector2, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.25 0.75" } });
    customizedUvGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector2, 2U, "xy", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "customizedUv0"));
    const RenderMaterialGraphCompileResult customizedUvResult = CompileRenderMaterialGraphToShaderSource(customizedUvGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0671U });
    Require(customizedUvResult.Succeeded(), "KBMAT-MAT67: CustomizedUV0 graph must compile");
    Require(customizedUvResult.shader.reflection.hasCustomizedUv0 &&
            customizedUvResult.shader.source.find("vec2 EvaluateCustomizedUv0") != std::string::npos,
        "KBMAT-MAT67: CustomizedUV0 must set reflection and emit a vertex evaluation function");
    Require(RenderMaterialGraphVariantKey(baseResult.shader) != RenderMaterialGraphVariantKey(customizedUvResult.shader),
        "KBMAT-MAT67: CustomizedUV0 must participate in graph variant identity");

    RenderMaterialGraphDocument displacementGraph = MakeDefaultRenderMaterialGraphDocument();
    displacementGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantVector, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.1 0 0" } });
    displacementGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantVector, 2U, "xyz", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "displacement"));
    const RenderMaterialGraphCompileResult displacementResult = CompileRenderMaterialGraphToShaderSource(displacementGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0672U });
    Require(displacementResult.Succeeded(), "KBMAT-MAT67: Displacement graph must compile");
    Require(displacementResult.shader.reflection.hasDisplacement &&
            displacementResult.shader.source.find("vec3 EvaluateDisplacement") != std::string::npos,
        "KBMAT-MAT67: Displacement must set reflection and emit a vertex evaluation function");
    Require(RenderMaterialGraphVariantKey(baseResult.shader) != RenderMaterialGraphVariantKey(displacementResult.shader),
        "KBMAT-MAT67: Displacement must participate in graph variant identity");
    const std::string displacedFragment = BuildGraphFragmentWrapperSource(displacementResult.shader, "BaseOpaque");
    Require(displacedFragment.find("dFdx(v_worldPos)") != std::string::npos &&
            displacedFragment.find("dFdy(v_worldPos)") != std::string::npos,
        "KBMAT-MAT67: DefaultLit fragment wrapper must recalculate normals from deformed world position");

    const RenderMaterialGraphMaterialTypeBuildResult materialType = BuildRenderMaterialGraphMaterialTypeDocument(
        displacementGraph, "test.vertex.domain", 1U, RenderMaterialGraphBuildContext{ .assetId = 0x0673U });
    Require(materialType.Succeeded() && materialType.document.has_value(), "KBMAT-MAT67: vertex-domain graph material type must build");
    Require(hasPin(materialType.document->schema.unsupportedAdvancedFeatures, "tessellation"),
        "KBMAT-MAT67: tessellation must be listed as non-production instead of implied as supported");
}

void RunMaterialVariantSwitchPermutationTest() {
    Require(ParseRenderMaterialGraphNodeKind("QualitySwitch") == RenderMaterialGraphNodeKind::QualitySwitch,
        "KBMAT-MAT52: QualitySwitch must parse from graph assets");
    Require(ParseRenderMaterialGraphNodeKind("FeatureLevelSwitch") == RenderMaterialGraphNodeKind::FeatureLevelSwitch,
        "KBMAT-MAT52: FeatureLevelSwitch must parse from graph assets");
    Require(ParseRenderMaterialGraphNodeKind("ShadingPathSwitch") == RenderMaterialGraphNodeKind::ShadingPathSwitch,
        "KBMAT-MAT52: ShadingPathSwitch must parse from graph assets");
    Require(ParseRenderMaterialGraphNodeKind("ShaderStageSwitch") == RenderMaterialGraphNodeKind::ShaderStageSwitch,
        "KBMAT-MAT52: ShaderStageSwitch must parse from graph assets");

    const auto hasPin = [](const std::vector<std::string>& pins, std::string_view pin) {
        return std::find(pins.begin(), pins.end(), pin) != pins.end();
    };
    const std::vector<std::string> qualityInputs = RenderMaterialGraphNodeInputPinNames(RenderMaterialGraphNodeKind::QualitySwitch);
    Require(hasPin(qualityInputs, "low") && hasPin(qualityInputs, "med") && hasPin(qualityInputs, "high") && hasPin(qualityInputs, "epic"),
        "KBMAT-MAT52: QualitySwitch must expose low/med/high/epic input pins");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::QualitySwitch, "result", true) == RenderMaterialGraphPinType::Float4,
        "KBMAT-MAT52: QualitySwitch result must be a Float4 branch value");
    Require(RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::QualitySwitch, "epic", false) != 0U &&
            RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::ShaderStageSwitch, "fragment", false) != 0U,
        "KBMAT-MAT52: variant switch pins must have stable ids");

    const auto makeConstant = [](std::uint32_t id, const char* value) {
        return RenderMaterialGraphNode{ .id = id, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = value } };
    };
    const auto buildQualityGraph = [&makeConstant]() {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.nodes.push_back(makeConstant(2U, "1 0 0 1"));
        graph.nodes.push_back(makeConstant(3U, "0 0 1 1"));
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::QualitySwitch });
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::QualitySwitch, 4U, "low"));
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::QualitySwitch, 4U, "high"));
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::QualitySwitch, 4U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return graph;
    };

    const RenderMaterialGraphCompileResult lowResult = CompileRenderMaterialGraphToShaderSource(
        buildQualityGraph(), RenderMaterialGraphBuildContext{ .assetId = 0x0520U, .qualityLevel = RenderMaterialGraphQualityLevel::Low });
    const RenderMaterialGraphCompileResult highResult = CompileRenderMaterialGraphToShaderSource(
        buildQualityGraph(), RenderMaterialGraphBuildContext{ .assetId = 0x0521U, .qualityLevel = RenderMaterialGraphQualityLevel::High });
    Require(lowResult.Succeeded() && highResult.Succeeded(), "KBMAT-MAT52: QualitySwitch graph must compile for multiple quality levels");
    Require(lowResult.shader.sourceHash != highResult.shader.sourceHash,
        "KBMAT-MAT52: changing quality must produce a different graph shader variant");
    Require(lowResult.shader.source.find("vec4(1.0, 0.0, 0.0, 1.0)") != std::string::npos &&
            lowResult.shader.source.find("vec4(0.0, 0.0, 1.0, 1.0)") == std::string::npos,
        "KBMAT-MAT52: low quality must compile only the low branch");
    Require(highResult.shader.source.find("vec4(0.0, 0.0, 1.0, 1.0)") != std::string::npos &&
            highResult.shader.source.find("vec4(1.0, 0.0, 0.0, 1.0)") == std::string::npos,
        "KBMAT-MAT52: high quality must compile only the high branch");

    const RenderMaterialGraphCompileArtifactCacheKey lowKey = BuildRenderMaterialGraphCompileArtifactCacheKey(
        buildQualityGraph(),
        std::span<const RenderMaterialGraphDependencyHashInput>{},
        0U,
        RenderMaterialGraphBuildContext{ .assetId = 0x0522U, .qualityLevel = RenderMaterialGraphQualityLevel::Low });
    const RenderMaterialGraphCompileArtifactCacheKey highKey = BuildRenderMaterialGraphCompileArtifactCacheKey(
        buildQualityGraph(),
        std::span<const RenderMaterialGraphDependencyHashInput>{},
        0U,
        RenderMaterialGraphBuildContext{ .assetId = 0x0522U, .qualityLevel = RenderMaterialGraphQualityLevel::High });
    Require(lowKey.combinedHash != highKey.combinedHash && lowKey.graphContentHash == highKey.graphContentHash,
        "KBMAT-MAT52: artifact cache key must vary by quality without changing the graph content hash");

    const RenderMaterialGraphMaterialTypeBuildResult typeResult = BuildRenderMaterialGraphMaterialTypeDocument(
        buildQualityGraph(), "test.variant.quality", 1U, RenderMaterialGraphBuildContext{ .assetId = 0x0523U, .qualityLevel = RenderMaterialGraphQualityLevel::Epic });
    Require(typeResult.Succeeded() && typeResult.document.has_value(), "KBMAT-MAT52: graph material type with QualitySwitch must build");
    const auto hasPermutation = [&typeResult](std::string_view name, std::string_view defaultValue) {
        for (const RenderMaterialTypePermutationKey& key : typeResult.document->permutationKeys) {
            if (key.name == name && key.defaultValue == defaultValue) {
                return true;
            }
        }
        return false;
    };
    Require(hasPermutation("qualityLevel", "epic"), "KBMAT-MAT52: material type must expose a qualityLevel permutation key");

    RenderMaterialGraphDocument stageGraph = MakeDefaultRenderMaterialGraphDocument();
    stageGraph.nodes.push_back(makeConstant(2U, "1 0 0 1"));
    stageGraph.nodes.push_back(makeConstant(3U, "0 0 1 1"));
    stageGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::ShaderStageSwitch });
    stageGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::ShaderStageSwitch, 4U, "vertex"));
    stageGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::ShaderStageSwitch, 4U, "fragment"));
    stageGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ShaderStageSwitch, 4U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    stageGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ShaderStageSwitch, 4U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "worldPositionOffset"));
    const RenderMaterialGraphCompileResult stageResult = CompileRenderMaterialGraphToShaderSource(stageGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0524U });
    Require(stageResult.Succeeded(), "KBMAT-MAT52: ShaderStageSwitch graph must compile in fragment and vertex codegen");
    const std::size_t surfacePos = stageResult.shader.source.find("MaterialSurface EvaluateMaterialGraph");
    const std::size_t wpoPos = stageResult.shader.source.find("vec3 EvaluateWorldPositionOffset");
    Require(surfacePos != std::string::npos && wpoPos != std::string::npos && surfacePos < wpoPos,
        "KBMAT-MAT52: ShaderStageSwitch WPO graph must emit both material and WPO functions");
    const std::string surfaceSection = stageResult.shader.source.substr(surfacePos, wpoPos - surfacePos);
    const std::string wpoSection = stageResult.shader.source.substr(wpoPos);
    Require(surfaceSection.find("vec4(0.0, 0.0, 1.0, 1.0)") != std::string::npos &&
            surfaceSection.find("vec4(1.0, 0.0, 0.0, 1.0)") == std::string::npos,
        "KBMAT-MAT52: ShaderStageSwitch must select the fragment branch inside EvaluateMaterialGraph");
    Require(wpoSection.find("vec4(1.0, 0.0, 0.0, 1.0)") != std::string::npos &&
            wpoSection.find("vec4(0.0, 0.0, 1.0, 1.0)") == std::string::npos,
        "KBMAT-MAT52: ShaderStageSwitch must select the vertex branch inside EvaluateWorldPositionOffset");
}

void RunMaterialCoordinateNodeCodegenTest() {
    // ConstantBiasScale: (input + bias) * scale, per channel.
    RenderMaterialGraphDocument biasGraph = MakeDefaultRenderMaterialGraphDocument();
    biasGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ConstantColor, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5 0.5 0.5 1" } });
    biasGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::ConstantBiasScale, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.25 2" } });
    biasGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::ConstantBiasScale, 3U, "input"));
    biasGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantBiasScale, 3U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult biasResult = CompileRenderMaterialGraphToShaderSource(biasGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0450U });
    Require(biasResult.Succeeded(), "KBMAT-MAT45: ConstantBiasScale graph must compile");
    Require(biasResult.shader.source.find("vec4_splat(0.25)") != std::string::npos && biasResult.shader.source.find("vec4_splat(2.0)") != std::string::npos,
        "KBMAT-MAT45: ConstantBiasScale must emit (input + bias) * scale");

    // BumpOffset: parallax along the view direction.
    RenderMaterialGraphDocument bumpGraph = MakeDefaultRenderMaterialGraphDocument();
    bumpGraph.shadingModel = "unlit";
    bumpGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::BumpOffset, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.05" } });
    bumpGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample, .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "bump", .textureRole = "baseColor", .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb } });
    bumpGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::BumpOffset, 2U, "uv", RenderMaterialGraphNodeKind::TextureSample, 3U, "uv"));
    bumpGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult bumpResult = CompileRenderMaterialGraphToShaderSource(bumpGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0451U });
    Require(bumpResult.Succeeded() && bumpResult.shader.source.find("ctx.viewDir.xy") != std::string::npos,
        "KBMAT-MAT45: BumpOffset must offset along the tangent-space view direction");

    // RotateAboutAxis: Rodrigues rotation feeding the normal output.
    RenderMaterialGraphDocument axisGraph = MakeDefaultRenderMaterialGraphDocument();
    axisGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::RotateAboutAxis });
    axisGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::RotateAboutAxis, 2U, "result", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "normal"));
    const RenderMaterialGraphCompileResult axisResult = CompileRenderMaterialGraphToShaderSource(axisGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0452U });
    Require(axisResult.Succeeded() && axisResult.shader.source.find("cross(") != std::string::npos && axisResult.shader.source.find("normalize(") != std::string::npos,
        "KBMAT-MAT45: RotateAboutAxis must emit a Rodrigues rotation (cross + normalize)");

    // ViewportUV: the normalised viewport coordinate.
    RenderMaterialGraphDocument viewportGraph = MakeDefaultRenderMaterialGraphDocument();
    viewportGraph.shadingModel = "unlit";
    viewportGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ViewportUV });
    viewportGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample, .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "vp", .textureRole = "baseColor", .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb } });
    viewportGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ViewportUV, 2U, "uv", RenderMaterialGraphNodeKind::TextureSample, 3U, "uv"));
    viewportGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult viewportResult = CompileRenderMaterialGraphToShaderSource(viewportGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0453U });
    Require(viewportResult.Succeeded() && viewportResult.shader.source.find("ctx.screenPosition") != std::string::npos,
        "KBMAT-MAT45: ViewportUV must emit the normalised screen coordinate");
}

void RunMaterialWorldSpaceNodeCodegenTest() {
    // Float3 world-space nodes routed to baseColor; each must emit its context expression.
    const auto compileVec3Node = [](RenderMaterialGraphNodeKind kind) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = kind });
        graph.links.push_back(MakeGraphLink(kind, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0460U });
    };
    const RenderMaterialGraphCompileResult cameraPos = compileVec3Node(RenderMaterialGraphNodeKind::CameraPosition);
    const RenderMaterialGraphCompileResult cameraVec = compileVec3Node(RenderMaterialGraphNodeKind::CameraVector);
    const RenderMaterialGraphCompileResult reflectVec = compileVec3Node(RenderMaterialGraphNodeKind::ReflectionVector);
    const RenderMaterialGraphCompileResult lightVec = compileVec3Node(RenderMaterialGraphNodeKind::LightVector);
    const RenderMaterialGraphCompileResult pixelNormal = compileVec3Node(RenderMaterialGraphNodeKind::PixelNormalWS);
    const RenderMaterialGraphCompileResult vertexTangent = compileVec3Node(RenderMaterialGraphNodeKind::VertexTangentWS);
    const RenderMaterialGraphCompileResult objectOrientation = compileVec3Node(RenderMaterialGraphNodeKind::ObjectOrientation);
    Require(cameraPos.Succeeded() && cameraPos.shader.source.find("ctx.cameraPosition") != std::string::npos, "KBMAT-MAT46: CameraPosition must emit ctx.cameraPosition");
    Require(cameraVec.Succeeded() && cameraVec.shader.source.find("ctx.viewDir") != std::string::npos, "KBMAT-MAT46: CameraVector must emit ctx.viewDir");
    Require(reflectVec.Succeeded() && reflectVec.shader.source.find("reflect(-ctx.viewDir, ctx.normal)") != std::string::npos, "KBMAT-MAT46: ReflectionVector must emit a reflect()");
    Require(lightVec.Succeeded() && lightVec.shader.source.find("ctx.lightVector") != std::string::npos, "KBMAT-MAT46: LightVector must emit ctx.lightVector");
    Require(pixelNormal.Succeeded() && pixelNormal.shader.source.find("ctx.normal") != std::string::npos, "KBMAT-MAT46: PixelNormalWS must emit ctx.normal");
    Require(vertexTangent.Succeeded() && vertexTangent.shader.source.find("ctx.tangent") != std::string::npos, "KBMAT-MAT46: VertexTangentWS must emit ctx.tangent");
    Require(objectOrientation.Succeeded() && objectOrientation.shader.source.find("ctx.objectOrientation") != std::string::npos, "KBMAT-MAT46: ObjectOrientation must emit ctx.objectOrientation");

    RenderMaterialGraphDocument boundsGraph = MakeDefaultRenderMaterialGraphDocument();
    boundsGraph.shadingModel = "unlit";
    boundsGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::ObjectBounds });
    boundsGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ObjectBounds, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult objectBounds = CompileRenderMaterialGraphToShaderSource(boundsGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0462U });
    Require(objectBounds.Succeeded() && objectBounds.shader.source.find("ctx.objectBounds") != std::string::npos, "KBMAT-MAT46: ObjectBounds must emit ctx.objectBounds");

    // Float2 screen/view nodes routed through a TextureSample UV pin.
    const auto compileVec2Node = [](RenderMaterialGraphNodeKind kind, std::string_view outputPin, std::string_view valueHint = {}) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = kind, .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = std::string{ valueHint } } });
        graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample, .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "vs", .textureRole = "baseColor", .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb } });
        graph.links.push_back(MakeGraphLink(kind, 2U, std::string{ outputPin }, RenderMaterialGraphNodeKind::TextureSample, 3U, "uv"));
        graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0461U });
    };
    const RenderMaterialGraphCompileResult pixelPosition = compileVec2Node(RenderMaterialGraphNodeKind::PixelPosition, "xy");
    const RenderMaterialGraphCompileResult viewSize = compileVec2Node(RenderMaterialGraphNodeKind::ViewSize, "value");
    const RenderMaterialGraphCompileResult viewProperty = compileVec2Node(RenderMaterialGraphNodeKind::ViewProperty, "value");
    const RenderMaterialGraphCompileResult viewPropertyInvSize = compileVec2Node(RenderMaterialGraphNodeKind::ViewProperty, "value", "invViewSize");
    const RenderMaterialGraphCompileResult viewPropertyScreenPosition = compileVec2Node(RenderMaterialGraphNodeKind::ViewProperty, "value", "screenPosition");
    const RenderMaterialGraphCompileResult viewPropertyPixelPosition = compileVec2Node(RenderMaterialGraphNodeKind::ViewProperty, "value", "pixelPosition");
    Require(pixelPosition.Succeeded() && pixelPosition.shader.source.find("ctx.screenPosition * ctx.viewSize") != std::string::npos,
        "KBMAT-MAT46: PixelPosition must emit absolute viewport pixel coordinates");
    Require(viewSize.Succeeded() && viewSize.shader.source.find("ctx.viewSize") != std::string::npos, "KBMAT-MAT46: ViewSize must emit ctx.viewSize");
    Require(viewProperty.Succeeded() && viewProperty.shader.source.find("ctx.viewSize") != std::string::npos, "KBMAT-MAT46: ViewProperty must emit a view property (ctx.viewSize)");
    Require(viewPropertyInvSize.Succeeded() && viewPropertyInvSize.shader.source.find("1.0, 1.0) / max(ctx.viewSize") != std::string::npos,
        "KBMAT-MAT46: ViewProperty must emit inverse view size when selected");
    Require(viewPropertyScreenPosition.Succeeded() && viewPropertyScreenPosition.shader.source.find("ctx.screenPosition") != std::string::npos,
        "KBMAT-MAT46: ViewProperty must emit screen position when selected");
    Require(viewPropertyPixelPosition.Succeeded() && viewPropertyPixelPosition.shader.source.find("ctx.screenPosition * ctx.viewSize") != std::string::npos,
        "KBMAT-MAT46: ViewProperty must emit pixel position when selected");

    RenderMaterialGraphDocument twoSidedSignGraph = MakeDefaultRenderMaterialGraphDocument();
    twoSidedSignGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::TwoSidedSign });
    twoSidedSignGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TwoSidedSign, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));
    const RenderMaterialGraphCompileResult twoSidedSign = CompileRenderMaterialGraphToShaderSource(twoSidedSignGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0463U });
    Require(twoSidedSign.Succeeded() && twoSidedSign.shader.source.find("ctx.twoSidedSign") != std::string::npos,
        "KBMAT-MAT46: TwoSidedSign must emit ctx.twoSidedSign");
}

void RunMaterialGraphReflectionUniformAndTextureCountTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "tintColor", .displayName = "Tint Color", .defaultValueHint = "1 1 1 1" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = 120,
        .positionY = 180,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "roughnessScale", .displayName = "Roughness Scale", .defaultValueHint = "0.5" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 120,
        .positionY = 280,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "albedoTex",
            .displayName = "Albedo",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        },
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 4U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));

    const RenderMaterialGraphCompileResult result = CompileRenderMaterialGraphToShaderSource(
        graph, RenderMaterialGraphBuildContext{ .assetId = 0x0210U, .sourcePath = "/Game/Materials/ReflectionCount.kbmat" });
    Require(result.Succeeded(), "KBMAT-MAT02: Graph with parameter and texture nodes should compile");
    Require(result.shader.reflection.uniforms.size() == 2U,
        "KBMAT-MAT02: Reflection must describe both reachable parameter uniforms (color + scalar)");
    Require(result.shader.reflection.textures.size() == 1U,
        "KBMAT-MAT02: Reflection must describe the reachable inline TextureSample slot");
    Require(result.shader.reflection.textures[0].stableId == "albedoTex",
        "KBMAT-MAT02: Reflection texture stableId must match the node stableId");
    Require(result.shader.reflection.textures[0].slot == kRenderMaterialGraphTextureBaseSlot,
        "KBMAT-MAT02: First graph texture must be assigned the base sampler stage (6, above the builtin 0-5)");
    Require(result.shader.source.find("uniform vec4 u_tintColor_rgba;") != std::string::npos,
        "KBMAT-MAT02: Compiled source must declare ParameterColor uniform");
    Require(result.shader.source.find("uniform vec4 u_roughnessScale;") != std::string::npos,
        "KBMAT-MAT02: Compiled source must declare ParameterScalar uniform");
    Require(result.shader.source.find("SAMPLER2D(u_albedoTex_texture, 6);") != std::string::npos,
        "KBMAT-MAT02: Compiled source must declare inline TextureSample sampler at the graph base stage 6");
    Require(!result.shader.reflection.requiredVaryings.empty() &&
            result.shader.reflection.requiredVaryings[0] == "uv0",
        "KBMAT-MAT02: Reflection must list uv0 as required varying when TextureSample is present");
}

void RunMaterialGraphNormalMapTextureRoleInferenceTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 1 1 1" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "normalTex",
            .displayName = "Normal Texture",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
        },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::NormalUnpack,
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::NormalUnpack, 4U, "color"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::NormalUnpack, 4U, "normal", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "normal"));

    const std::vector<RenderMaterialGraphDiagnostic> diagnostics = ValidateRenderMaterialGraphDocument(graph);
    Require(!HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::InvalidColorSpaceRole),
        "KBMAT-MAT02: TextureSample feeding Normal Map must not be rejected for its editor default baseColor/sRGB metadata");

    const RenderMaterialGraphCompileResult result = CompileRenderMaterialGraphToShaderSource(
        graph, RenderMaterialGraphBuildContext{ .assetId = 0x0211U, .sourcePath = "/Game/Materials/NormalInference.kbmat" });
    Require(result.Succeeded(), "KBMAT-MAT02: TextureSample -> Normal Map -> Output Normal graph must compile");
    Require(result.shader.reflection.textures.size() == 1U &&
            result.shader.reflection.textures[0].stableId == "normalTex" &&
            result.shader.reflection.textures[0].role == "normal" &&
            result.shader.reflection.textures[0].colorSpace == RenderMaterialTextureColorSpace::Linear,
        "KBMAT-MAT02: TextureSample feeding Normal Map must be reflected as role=normal linear data");

    const RenderMaterialTypeSchema schema = BuildRenderMaterialGraphParameterSchema(graph, "normal.inference", 1U);
    Require(schema.textureSlots.size() == 1U &&
            schema.textureSlots[0].role == "normal" &&
            schema.textureSlots[0].expectedColorSpace == RenderMaterialTextureColorSpace::Linear,
        "KBMAT-MAT02: TextureSample feeding Output Normal must expose a normal/linear texture slot");

    const RenderMaterialGraphProgramBindingResult binding = BuildRenderMaterialGraphProgramBinding(
        0x0211U,
        1U,
        result.shader,
        std::span<const RenderMaterialGraphParameterValue>{});
    Require(binding.binding.textures.size() == 1U &&
            binding.binding.textures[0].role == "normal" &&
            binding.binding.textures[0].colorSpace == RenderTextureColorSpace::Linear,
        "KBMAT-MAT02: Runtime graph binding must preserve role=normal and request the linear texture resource for normal maps");

    RenderMaterialGraphDocument objectGraph = MakeDefaultRenderMaterialGraphDocument();
    objectGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 1 1 1" },
    });
    objectGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::TextureObject,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "normalObject",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
        },
    });
    objectGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::TextureSample });
    objectGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::NormalUnpack });
    objectGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    objectGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureObject, 3U, "texture", RenderMaterialGraphNodeKind::TextureSample, 4U, "texture"));
    objectGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 4U, "color", RenderMaterialGraphNodeKind::NormalUnpack, 5U, "color"));
    objectGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::NormalUnpack, 5U, "normal", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "normal"));

    const RenderMaterialGraphCompileResult objectResult = CompileRenderMaterialGraphToShaderSource(
        objectGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0212U, .sourcePath = "/Game/Materials/NormalObjectInference.kbmat" });
    Require(objectResult.Succeeded() &&
            objectResult.shader.reflection.textures.size() == 1U &&
            objectResult.shader.reflection.textures[0].stableId == "normalObject" &&
            objectResult.shader.reflection.textures[0].colorSpace == RenderMaterialTextureColorSpace::Linear,
        "KBMAT-MAT02: TextureObject feeding a Normal Map sample must also infer linear normal data");
}

void RunMaterialParameterCollectionAssetRoundTripTest() {
    RenderMaterialParameterCollectionData collection{};
    collection.displayName = "Global Material Controls";
    collection.parameters.push_back(RenderMaterialParameterCollectionParameter{
        .stableId = "GlobalTint",
        .displayName = "Global Tint",
        .type = RenderMaterialParameterCollectionValueType::Vector,
        .defaultValue = { 0.25F, 0.50F, 0.75F, 1.0F },
        .editorOrder = 7U,
        .description = "Scene-wide tint",
    });

    std::stringstream output;
    RenderMaterialParameterCollectionWriter::Write(output, collection);
    Require(output.str().find("collectionParameter GlobalTint Vector Global%20Tint 0.25 0.5 0.75 1 7 Scene-wide%20tint") != std::string::npos,
        "KBMAT-MAT50: Material Parameter Collection writer must emit stable ids, typed defaults and editor metadata");

    std::stringstream input{ output.str() };
    const RenderMaterialParameterCollectionParseResult parsed =
        RenderMaterialParameterCollectionAssetLoader::LoadCollectionWithDiagnostics(input);
    Require(parsed.Succeeded() && parsed.collection.has_value(),
        "KBMAT-MAT50: Material Parameter Collection writer output must parse back without diagnostics");
    Require(parsed.collection->displayName == collection.displayName &&
            parsed.collection->parameters.size() == 1U &&
            parsed.collection->parameters[0].stableId == "GlobalTint" &&
            parsed.collection->parameters[0].displayName == "Global Tint" &&
            parsed.collection->parameters[0].type == RenderMaterialParameterCollectionValueType::Vector &&
            NearlyEqual(parsed.collection->parameters[0].defaultValue[2], 0.75F) &&
            parsed.collection->parameters[0].editorOrder == 7U &&
            parsed.collection->parameters[0].description == "Scene-wide tint",
        "KBMAT-MAT50: Material Parameter Collection round-trip must preserve runtime and editor fields");

    std::stringstream duplicateInput;
    duplicateInput <<
        "version 1\n"
        "collectionParameter Duplicate Scalar Duplicate 0 0 0 0 0 _\n"
        "collectionParameter Duplicate Scalar Duplicate 1 0 0 0 1 _\n";
    const RenderMaterialParameterCollectionParseResult duplicate =
        RenderMaterialParameterCollectionAssetLoader::LoadCollectionWithDiagnostics(duplicateInput);
    Require(!duplicate.Succeeded() && !duplicate.collection.has_value() && !duplicate.diagnostics.empty(),
        "KBMAT-MAT50: Material Parameter Collection parser must reject duplicate stable ids");
}

void RunMaterialGraphCollectionParameterBindingTest() {
    constexpr std::uint64_t collectionAssetId = 0x5000CAFEU;
    constexpr std::uint64_t graphAssetId = 0x50010001U;

    RenderMaterialParameterCollectionData collection{};
    collection.displayName = "Scene Globals";
    collection.parameters.push_back(RenderMaterialParameterCollectionParameter{
        .stableId = "GlobalTint",
        .displayName = "Global Tint",
        .type = RenderMaterialParameterCollectionValueType::Vector,
        .defaultValue = { 0.20F, 0.40F, 0.60F, 1.0F },
        .editorOrder = 0U,
        .description = "Scene tint",
    });

    RenderMaterialParameterCollectionRuntimeStore& store = GlobalRenderMaterialParameterCollectionStore();
    store.Clear();
    Require(store.LoadDefaults(collectionAssetId, collection), "KBMAT-MAT50: Runtime MPC store must load collection defaults");

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::CollectionParameter,
        .positionX = 80,
        .positionY = 60,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "GlobalTint",
            .displayName = "Global Tint",
            .defaultValueHint = std::to_string(collectionAssetId),
            .overrideSupported = false,
        },
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::CollectionParameter, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const std::vector<RenderMaterialGraphDiagnostic> diagnostics = ValidateRenderMaterialGraphDocument(graph);
    Require(!HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed) &&
            !HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::DuplicateParameterStableId),
        "KBMAT-MAT50: Valid CollectionParameter metadata must pass graph validation");
    const std::vector<std::uint64_t> dependencies = DiscoverRenderMaterialGraphParameterCollectionDependencies(graph);
    Require(dependencies.size() == 1U && dependencies[0] == collectionAssetId,
        "KBMAT-MAT50: Graph dependency discovery must list Material Parameter Collection assets");
    Require(FindRenderMaterialGraphCollectionParameter(graph, collectionAssetId, "GlobalTint") != nullptr,
        "KBMAT-MAT50: Collection parameter lookup must find graph nodes by collection asset and stable id");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::CollectionParameter, "scalar", true) == RenderMaterialGraphPinType::Float &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::CollectionParameter, "xyz", true) == RenderMaterialGraphPinType::Float3 &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::CollectionParameter, "rgba", true) == RenderMaterialGraphPinType::Color,
        "KBMAT-MAT50: CollectionParameter must expose typed scalar/vector/color output pins");

    const RenderMaterialTypeSchema schema = BuildRenderMaterialGraphParameterSchema(graph, "mpc.schema", 1U);
    Require(schema.parameters.empty() && schema.textureSlots.empty(),
        "KBMAT-MAT50: CollectionParameter values are global and must not leak into per-material override schema");

    const RenderMaterialGraphCompileResult compiled =
        CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = graphAssetId });
    Require(compiled.Succeeded(), "KBMAT-MAT50: CollectionParameter graph must compile");
    Require(compiled.shader.reflection.uniforms.size() == 1U,
        "KBMAT-MAT50: CollectionParameter graph reflection must expose one uniform");
    const RenderMaterialGraphReflectionUniform& uniform = compiled.shader.reflection.uniforms[0];
    Require(uniform.source == RenderMaterialGraphReflectionUniformSource::ParameterCollection &&
            uniform.collectionAssetId == collectionAssetId &&
            uniform.collectionParameterStableId == "GlobalTint" &&
            uniform.stableId == "GlobalTint" &&
            uniform.kind == RenderMaterialGraphNodeKind::CollectionParameter,
        "KBMAT-MAT50: CollectionParameter reflection must preserve source collection metadata");
    Require(compiled.shader.source.find("uniform vec4 " + uniform.name + ";") != std::string::npos,
        "KBMAT-MAT50: CollectionParameter codegen must declare a real GPU uniform");

    const std::array<RenderMaterialGraphParameterValue, 0U> values{};
    const RenderMaterialGraphProgramBindingResult defaults =
        BuildRenderMaterialGraphProgramBinding(0x9911U, 3U, compiled.shader, values);
    Require(defaults.binding.active && defaults.binding.uniforms.size() == 1U &&
            defaults.binding.uniforms[0].source == RenderMaterialGraphUniformBindingSource::ParameterCollection &&
            defaults.binding.uniforms[0].collectionAssetId == collectionAssetId &&
            defaults.binding.uniforms[0].collectionParameterStableId == "GlobalTint" &&
            NearlyEqual(defaults.binding.uniforms[0].value[0], 0.20F) &&
            NearlyEqual(defaults.binding.uniforms[0].value[2], 0.60F),
        "KBMAT-MAT50: Program binding must source MPC uniform values from the runtime collection store");

    const std::uint64_t sourceHash = compiled.shader.sourceHash;
    Require(store.SetValue(collectionAssetId, "GlobalTint", RenderMaterialParameterCollectionValueType::Vector, { 0.90F, 0.10F, 0.30F, 1.0F }),
        "KBMAT-MAT50: Runtime MPC store must accept live parameter edits");
    const RenderMaterialGraphProgramBindingResult edited =
        BuildRenderMaterialGraphProgramBinding(0x9911U, 3U, compiled.shader, values);
    Require(edited.binding.graphSourceHash == sourceHash &&
            NearlyEqual(edited.binding.uniforms[0].value[0], 0.90F) &&
            NearlyEqual(edited.binding.uniforms[0].value[1], 0.10F) &&
            NearlyEqual(edited.binding.uniforms[0].value[2], 0.30F),
        "KBMAT-MAT50: MPC value edits must rebind uniforms without changing the graph shader program key");

    Require(store.RenameParameterStableId(collectionAssetId, "GlobalTint", "SceneTint"),
        "KBMAT-MAT50: Runtime MPC store must support stable-id rename for rebind");
    RenderMaterialGraphDocument renamedGraph = graph;
    RenderMaterialGraphNode* renamedNode = nullptr;
    for (RenderMaterialGraphNode& node : renamedGraph.nodes) {
        if (node.id == 2U) {
            renamedNode = &node;
            break;
        }
    }
    Require(renamedNode != nullptr, "KBMAT-MAT50: Test graph must contain the CollectionParameter node before rename");
    renamedNode->parameter.stableId = "SceneTint";
    renamedNode->parameter.displayName = "Scene Tint";
    const RenderMaterialGraphCompileResult renamed =
        CompileRenderMaterialGraphToShaderSource(renamedGraph, RenderMaterialGraphBuildContext{ .assetId = graphAssetId });
    Require(renamed.Succeeded() && renamed.shader.reflection.uniforms[0].collectionParameterStableId == "SceneTint",
        "KBMAT-MAT50: Renamed CollectionParameter graph must recompile with the new stable id");
    const RenderMaterialGraphProgramBindingResult renamedBinding =
        BuildRenderMaterialGraphProgramBinding(0x9911U, 3U, renamed.shader, values);
    Require(NearlyEqual(renamedBinding.binding.uniforms[0].value[0], 0.90F) &&
            NearlyEqual(renamedBinding.binding.uniforms[0].value[1], 0.10F) &&
            NearlyEqual(renamedBinding.binding.uniforms[0].value[2], 0.30F),
        "KBMAT-MAT50: Renamed MPC stable id must preserve the live runtime value after rebind");

    RenderMaterialGraphDocument invalid = graph;
    RenderMaterialGraphNode* invalidNode = nullptr;
    for (RenderMaterialGraphNode& node : invalid.nodes) {
        if (node.id == 2U) {
            invalidNode = &node;
            break;
        }
    }
    Require(invalidNode != nullptr, "KBMAT-MAT50: Test graph must contain the CollectionParameter node before invalid-id validation");
    invalidNode->parameter.defaultValueHint = "0";
    const std::vector<RenderMaterialGraphDiagnostic> invalidDiagnostics = ValidateRenderMaterialGraphDocument(invalid);
    Require(HasGraphDiagnostic(invalidDiagnostics, RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed),
        "KBMAT-MAT50: CollectionParameter without a collection asset id must report a graph diagnostic");

    store.Clear();
    const RenderMaterialGraphProgramBindingResult missingRuntimeValue =
        BuildRenderMaterialGraphProgramBinding(0x9911U, 3U, compiled.shader, values);
    Require(HasGraphDiagnostic(missingRuntimeValue.diagnostics, RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed),
        "KBMAT-MAT50: Missing runtime MPC values must produce a diagnostic instead of a silent zero fallback");

    store.Clear();
}

void RunMaterialGraphTextureExpansionSchemaTest() {
    Require(ParseRenderMaterialGraphNodeKind("TextureObject").value_or(RenderMaterialGraphNodeKind::MaterialOutput) == RenderMaterialGraphNodeKind::TextureObject,
        "KBMAT-MAT31: parser must recognize TextureObject");
    Require(ParseRenderMaterialGraphNodeKind("TextureSampleCube").value_or(RenderMaterialGraphNodeKind::MaterialOutput) == RenderMaterialGraphNodeKind::TextureSampleCube,
        "KBMAT-MAT31: parser must recognize TextureSampleCube");
    Require(ParseRenderMaterialGraphNodeKind("TextureSample3D").value_or(RenderMaterialGraphNodeKind::MaterialOutput) == RenderMaterialGraphNodeKind::TextureSampleVolume,
        "KBMAT-MAT31: parser must recognize TextureSample3D alias");
    Require(ParseRenderMaterialGraphNodeKind("TextureSample2DArray").value_or(RenderMaterialGraphNodeKind::MaterialOutput) == RenderMaterialGraphNodeKind::TextureSample2DArray,
        "KBMAT-MAT31: parser must recognize TextureSample2DArray");
    Require(ParseRenderMaterialGraphNodeKind("SceneColor").value_or(RenderMaterialGraphNodeKind::MaterialOutput) == RenderMaterialGraphNodeKind::SceneColor,
        "KBMAT-MAT31: parser must recognize SceneColor");
    Require(ParseRenderMaterialGraphNodeKind("SceneTexture").value_or(RenderMaterialGraphNodeKind::MaterialOutput) == RenderMaterialGraphNodeKind::SceneTexture,
        "KBMAT-MAT31: parser must recognize SceneTexture");
    Require(ParseRenderMaterialGraphNodeKind("PixelDepth").value_or(RenderMaterialGraphNodeKind::MaterialOutput) == RenderMaterialGraphNodeKind::PixelDepth,
        "KBMAT-MAT31: parser must recognize PixelDepth");
    Require(ParseRenderMaterialGraphNodeKind("PixelPosition").value_or(RenderMaterialGraphNodeKind::MaterialOutput) == RenderMaterialGraphNodeKind::PixelPosition,
        "KBMAT-MAT31: parser must recognize PixelPosition");
    Require(ParseRenderMaterialGraphNodeKind("CameraDepthFade").value_or(RenderMaterialGraphNodeKind::MaterialOutput) == RenderMaterialGraphNodeKind::CameraDepthFade,
        "KBMAT-MAT31: parser must recognize CameraDepthFade");

    Require(IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::TextureSampleCube, "direction") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::TextureSampleCube, "texture", false) == RenderMaterialGraphPinType::TextureCube,
        "KBMAT-MAT31: TextureSampleCube must expose a textureCube input and direction input");
    Require(IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::TextureSampleVolume, "uvw") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::TextureSampleVolume, "texture", false) == RenderMaterialGraphPinType::Texture3D,
        "KBMAT-MAT31: TextureSampleVolume must expose a texture3D input and uvw input");
    Require(IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::TextureSample2DArray, "layer") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::TextureSample2DArray, "texture", false) == RenderMaterialGraphPinType::Texture2DArray,
        "KBMAT-MAT31: TextureSample2DArray must expose a texture2DArray input and layer input");
    Require(IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::SceneColor, "r") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::SceneTexture, "color", true) == RenderMaterialGraphPinType::Color,
        "KBMAT-MAT31: SceneColor/SceneTexture must expose color channel outputs");
    Require(IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::PixelDepth, "value") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::PixelDepth, "value", true) == RenderMaterialGraphPinType::Float,
        "KBMAT-MAT31: PixelDepth must expose a float value output");
    Require(IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::PixelPosition, "xy") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::PixelPosition, "xy", true) == RenderMaterialGraphPinType::Float2,
        "KBMAT-MAT31: PixelPosition must expose a float2 xy output");
    Require(IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::CameraDepthFade, "fadeLength") &&
            IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::CameraDepthFade, "fadeOffset") &&
            IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::CameraDepthFade, "value") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::CameraDepthFade, "value", true) == RenderMaterialGraphPinType::Float,
        "KBMAT-MAT31: CameraDepthFade must expose fade controls and a float value output");

    RenderMaterialGraphDocument objectGraph = MakeDefaultRenderMaterialGraphDocument();
    objectGraph.shadingModel = "unlit";
    objectGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::TextureObject,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "objectTex", .textureRole = "baseColor", .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb },
    });
    objectGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample });
    objectGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureObject, 2U, "texture", RenderMaterialGraphNodeKind::TextureSample, 3U, "texture"));
    objectGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult objectResult = CompileRenderMaterialGraphToShaderSource(objectGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0490U });
    Require(objectResult.Succeeded(), "KBMAT-MAT31: TextureObject -> TextureSample graph must compile");
    Require(objectResult.shader.reflection.textures.size() == 1U &&
            objectResult.shader.reflection.textures[0].stableId == "objectTex" &&
            objectResult.shader.reflection.textures[0].dimension == RenderMaterialGraphTextureDimension::Texture2D,
        "KBMAT-MAT31: TextureObject must contribute a 2D sampler to reflection");
    Require(objectResult.shader.source.find("SAMPLER2D(u_objectTex_texture, 6);") != std::string::npos,
        "KBMAT-MAT31: TextureObject sampler must be declared in generated shader source");

    const auto compileSingleSample = [](RenderMaterialGraphNodeKind kind, const char* stableId) {
        RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
        graph.shadingModel = "unlit";
        graph.nodes.push_back(RenderMaterialGraphNode{
            .id = 2U,
            .kind = kind,
            .parameter = RenderMaterialGraphParameterMetadata{ .stableId = stableId, .textureRole = "baseColor", .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb },
        });
        graph.links.push_back(MakeGraphLink(kind, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
        return CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0491U });
    };
    const RenderMaterialGraphCompileResult cube = compileSingleSample(RenderMaterialGraphNodeKind::TextureSampleCube, "cubeTex");
    const RenderMaterialGraphCompileResult volume = compileSingleSample(RenderMaterialGraphNodeKind::TextureSampleVolume, "volumeTex");
    const RenderMaterialGraphCompileResult array = compileSingleSample(RenderMaterialGraphNodeKind::TextureSample2DArray, "arrayTex");
    Require(cube.Succeeded() && cube.shader.source.find("SAMPLERCUBE(u_cubeTex_texture, 6);") != std::string::npos &&
            cube.shader.reflection.textures[0].dimension == RenderMaterialGraphTextureDimension::TextureCube,
        "KBMAT-MAT31: cube texture samples must declare a cube sampler and reflection dimension");
    Require(volume.Succeeded() && volume.shader.source.find("SAMPLER3D(u_volumeTex_texture, 6);") != std::string::npos &&
            volume.shader.reflection.textures[0].dimension == RenderMaterialGraphTextureDimension::Texture3D,
        "KBMAT-MAT31: volume texture samples must declare a 3D sampler and reflection dimension");
    Require(array.Succeeded() && array.shader.source.find("SAMPLER2DARRAY(u_arrayTex_texture, 6);") != std::string::npos &&
            array.shader.reflection.textures[0].dimension == RenderMaterialGraphTextureDimension::Texture2DArray,
        "KBMAT-MAT31: 2D array texture samples must declare an array sampler and reflection dimension");

    RenderMaterialGraphDocument sceneColorGraph = MakeDefaultRenderMaterialGraphDocument();
    sceneColorGraph.shadingModel = "unlit";
    sceneColorGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::SceneColor });
    sceneColorGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::SceneColor, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult sceneColor = CompileRenderMaterialGraphToShaderSource(sceneColorGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0492U });
    Require(sceneColor.Succeeded() && sceneColor.shader.reflection.usesSceneColor && !sceneColor.shader.reflection.usesSceneDepth,
        "KBMAT-MAT31: SceneColor must set usesSceneColor reflection");

    RenderMaterialGraphDocument sceneDepthTextureGraph = MakeDefaultRenderMaterialGraphDocument();
    sceneDepthTextureGraph.shadingModel = "unlit";
    RenderMaterialGraphNode sceneTextureDepth{ .id = 2U, .kind = RenderMaterialGraphNodeKind::SceneTexture };
    sceneTextureDepth.parameter.defaultValueHint = "depth";
    sceneDepthTextureGraph.nodes.push_back(sceneTextureDepth);
    sceneDepthTextureGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::SceneTexture, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult sceneTextureDepthResult = CompileRenderMaterialGraphToShaderSource(sceneDepthTextureGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0493U });
    Require(sceneTextureDepthResult.Succeeded() && sceneTextureDepthResult.shader.reflection.usesSceneDepth && !sceneTextureDepthResult.shader.reflection.usesSceneColor,
        "KBMAT-MAT31: SceneTexture(depth) must set usesSceneDepth reflection");

    RenderMaterialGraphDocument pixelDepthGraph = MakeDefaultRenderMaterialGraphDocument();
    pixelDepthGraph.shadingModel = "unlit";
    pixelDepthGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::PixelDepth });
    pixelDepthGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::PixelDepth, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));
    const RenderMaterialGraphCompileResult pixelDepth = CompileRenderMaterialGraphToShaderSource(pixelDepthGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0494U });
    Require(pixelDepth.Succeeded() &&
            pixelDepth.shader.source.find("ctx.fragmentDepth") != std::string::npos &&
            pixelDepth.shader.source.find("SAMPLER2D(s_kbSceneDepth") == std::string::npos &&
            !pixelDepth.shader.reflection.usesSceneDepth,
        "KBMAT-MAT31: PixelDepth must compile from fragment depth without requiring scene-depth texture binding");

    RenderMaterialGraphDocument cameraDepthFadeGraph = MakeDefaultRenderMaterialGraphDocument();
    cameraDepthFadeGraph.shadingModel = "unlit";
    cameraDepthFadeGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::CameraDepthFade });
    cameraDepthFadeGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::CameraDepthFade, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));
    const RenderMaterialGraphCompileResult cameraDepthFade = CompileRenderMaterialGraphToShaderSource(cameraDepthFadeGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0495U });
    Require(cameraDepthFade.Succeeded() &&
            cameraDepthFade.shader.source.find("distance(ctx.cameraPosition, ctx.worldPos)") != std::string::npos &&
            cameraDepthFade.shader.source.find("SAMPLER2D(s_kbSceneDepth") == std::string::npos &&
            !cameraDepthFade.shader.reflection.usesSceneDepth,
        "KBMAT-MAT31: CameraDepthFade must compile from camera/world context without requiring scene-depth texture binding");

    RenderMaterialGraphDocument mismatchGraph = MakeDefaultRenderMaterialGraphDocument();
    mismatchGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::TextureObjectCube });
    mismatchGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::TextureSample });
    mismatchGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureObjectCube, 2U, "texture", RenderMaterialGraphNodeKind::TextureSample, 3U, "texture"));
    mismatchGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const std::vector<RenderMaterialGraphDiagnostic> diagnostics = ValidateRenderMaterialGraphDocument(mismatchGraph);
    Require(HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::TypeMismatch),
        "KBMAT-MAT31: mismatched texture object/sample dimensions must be rejected by validation");

    const RenderMaterialTypeSchema schema = BuildRenderMaterialGraphParameterSchema(objectGraph, "mat31.schema", 1U);
    Require(schema.textureSlots.size() == 1U && schema.textureSlots[0].assetIdFieldName == "objectTexTextureAssetId",
        "KBMAT-MAT31: TextureObject slots must be emitted into generated material schema");

    const std::array<RenderMaterialGraphParameterValue, 1U> values{
        RenderMaterialGraphParameterValue{ .stableId = "objectTex", .type = RenderMaterialParameterType::Texture, .assetId = 0xBB01U },
    };
    const RenderMaterialGraphProgramBindingResult binding = BuildRenderMaterialGraphProgramBinding(0x9911U, 3U, objectResult.shader, values);
    Require(binding.binding.active && binding.binding.textures.size() == 1U &&
            binding.binding.textures[0].dimension == RenderMaterialGraphTextureDimension::Texture2D &&
            binding.binding.textures[0].colorSpace == RenderTextureColorSpace::Srgb,
        "KBMAT-MAT31: program binding must preserve texture dimension and color-space");
}

void RunMaterialGraphCustomCodeNodeSchemaTest() {
    Require(ParseRenderMaterialGraphNodeKind("CustomCode").value_or(RenderMaterialGraphNodeKind::MaterialOutput) == RenderMaterialGraphNodeKind::CustomCode,
        "KBMAT-MAT33: parser must recognize CustomCode");
    Require(IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::CustomCode, "A") &&
            IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind::CustomCode, "B") &&
            IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::CustomCode, "value"),
        "KBMAT-MAT33: CustomCode kind fallback must expose A/B/value pins");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::CustomCode, "value", true) == RenderMaterialGraphPinType::Float4,
        "KBMAT-MAT33: CustomCode kind fallback output must be float4");

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    RenderMaterialGraphNode custom{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::CustomCode,
        .positionX = 260,
        .positionY = 120,
        .customCode = RenderMaterialGraphCustomCode{
            .body = "Mask = B;\nreturn A * vec4_splat(B);",
            .outputType = RenderMaterialGraphPinType::Color,
            .inputs = {
                RenderMaterialGraphCustomPin{ .name = "A", .type = RenderMaterialGraphPinType::Color },
                RenderMaterialGraphCustomPin{ .name = "B", .type = RenderMaterialGraphPinType::Float },
            },
            .outputs = {
                RenderMaterialGraphCustomPin{ .name = "Mask", .type = RenderMaterialGraphPinType::Float },
            },
        },
    };
    RenderMaterialGraphNode tint{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.2 0.4 0.8 1", .overrideSupported = false },
    };
    RenderMaterialGraphNode scalar{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5", .overrideSupported = false },
    };
    const RenderMaterialGraphNode output = graph.nodes[0];
    graph.nodes.push_back(custom);
    graph.nodes.push_back(tint);
    graph.nodes.push_back(scalar);
    graph.links.push_back(MakeGraphLink(tint, "rgba", custom, "A"));
    graph.links.push_back(MakeGraphLink(scalar, "value", custom, "B"));
    graph.links.push_back(MakeGraphLink(custom, "value", output, "baseColor"));
    graph.links.push_back(MakeGraphLink(custom, "Mask", output, "roughness"));

    const std::vector<std::string> inputPins = RenderMaterialGraphNodeInputPinNames(custom);
    const std::vector<std::string> outputPins = RenderMaterialGraphNodeOutputPinNames(custom);
    Require(inputPins.size() == 2U && inputPins[0] == "A" && inputPins[1] == "B",
        "KBMAT-MAT33: node-aware CustomCode inputs must match the declared pins");
    Require(outputPins.size() == 2U && outputPins[0] == "value" && outputPins[1] == "Mask",
        "KBMAT-MAT33: node-aware CustomCode outputs must include value plus declared additional outputs");
    Require(RenderMaterialGraphPinDataType(custom, "A", false) == RenderMaterialGraphPinType::Color &&
            RenderMaterialGraphPinDataType(custom, "B", false) == RenderMaterialGraphPinType::Float &&
            RenderMaterialGraphPinDataType(custom, "Mask", true) == RenderMaterialGraphPinType::Float,
        "KBMAT-MAT33: node-aware CustomCode pin types must match declarations");

    const RenderMaterialGraphIrBuildResult ir = BuildRenderMaterialGraphIr(graph, RenderMaterialGraphBuildContext{ .assetId = 0x3300U });
    Require(ir.Succeeded(), "KBMAT-MAT33: CustomCode graph IR must validate");
    bool foundMaskOutput = false;
    for (const RenderMaterialGraphIrNode& node : ir.ir.nodes) {
        if (node.nodeId != custom.id) {
            continue;
        }
        for (const RenderMaterialGraphIrPin& pin : node.outputs) {
            if (pin.name == "Mask" && pin.type == RenderMaterialGraphPinType::Float && pin.stablePinId == RenderMaterialGraphStablePinId(custom, "Mask", true)) {
                foundMaskOutput = true;
            }
        }
    }
    Require(foundMaskOutput, "KBMAT-MAT33: CustomCode additional output must appear in typed IR");

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x3301U });
    Require(compiled.Succeeded(), "KBMAT-MAT33: valid CustomCode graph must generate shader source");
    Require(compiled.shader.source.find("vec4 kb_custom_2(vec4 A, float B, inout float Mask)") != std::string::npos &&
            compiled.shader.source.find("Mask = B;") != std::string::npos &&
            compiled.shader.source.find("custom2_value = kb_custom_2") != std::string::npos,
        "KBMAT-MAT33: CustomCode source must emit a typed helper function and call");

    RenderMaterialAssetData asset{};
    asset.graph = graph;
    std::ostringstream serialized;
    RenderMaterialAssetWriter::Write(serialized, asset);
    Require(serialized.str().find("graphCustomCode 2 color A:color,B:float Mask:float _ _ Mask%20=%20B;%0Areturn%20A%20*%20vec4_splat(B);") != std::string::npos,
        "KBMAT-MAT33: CustomCode metadata must serialize into the material asset");
    std::istringstream input{ serialized.str() };
    const RenderMaterialAssetParseResult parsed = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(parsed.Succeeded() && parsed.asset.has_value(), "KBMAT-MAT33: CustomCode material asset must parse after roundtrip");
    const RenderMaterialGraphNode* parsedCustom = FindRenderMaterialGraphNode(parsed.asset->graph, 2U);
    Require(parsedCustom != nullptr &&
            parsedCustom->customCode.body == custom.customCode.body &&
            parsedCustom->customCode.inputs.size() == 2U &&
            parsedCustom->customCode.outputs.size() == 1U &&
            parsedCustom->customCode.outputs[0].name == "Mask",
        "KBMAT-MAT33: CustomCode roundtrip must preserve body, inputs and outputs");

    RenderMaterialGraphDocument invalid = graph;
    RenderMaterialGraphNode* invalidCustom = nullptr;
    for (RenderMaterialGraphNode& node : invalid.nodes) {
        if (node.id == custom.id) {
            invalidCustom = &node;
            break;
        }
    }
    Require(invalidCustom != nullptr, "KBMAT-MAT33: test graph must contain CustomCode node");
    invalidCustom->customCode.inputs.push_back(RenderMaterialGraphCustomPin{ .name = "A", .type = RenderMaterialGraphPinType::Texture2D });
    const std::vector<RenderMaterialGraphDiagnostic> diagnostics = ValidateRenderMaterialGraphDocument(invalid);
    Require(HasGraphDiagnostic(diagnostics, RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed),
        "KBMAT-MAT33: duplicate/unsupported CustomCode pin declarations must be rejected");
}

void RunMaterialGraphVertexDataReflectionTest() {
    Require(ParseRenderMaterialGraphNodeKind("DistanceCullFade") == RenderMaterialGraphNodeKind::DistanceCullFade,
        "KBMAT-MAT47: DistanceCullFade must parse as a runtime fade input node");
    Require(IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind::DistanceCullFade, "value") &&
            RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::DistanceCullFade, "value", true) == RenderMaterialGraphPinType::Float,
        "KBMAT-MAT47: DistanceCullFade must expose a scalar value output");

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::VertexColor });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::PerInstanceRandom });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::PerInstanceFadeAmount });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::PerInstanceCustomData });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 6U, .kind = RenderMaterialGraphNodeKind::PreSkinnedPosition });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 7U, .kind = RenderMaterialGraphNodeKind::PreSkinnedNormal });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 8U, .kind = RenderMaterialGraphNodeKind::MakeVector });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 9U, .kind = RenderMaterialGraphNodeKind::DistanceCullFade });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::PerInstanceRandom, 3U, "value", RenderMaterialGraphNodeKind::MakeVector, 8U, "x"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::PerInstanceFadeAmount, 4U, "value", RenderMaterialGraphNodeKind::MakeVector, 8U, "y"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::PerInstanceCustomData, 5U, "value", RenderMaterialGraphNodeKind::MakeVector, 8U, "z"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::PreSkinnedPosition, 6U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::PreSkinnedNormal, 7U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "normal"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::VertexColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::MakeVector, 8U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::DistanceCullFade, 9U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));

    const RenderMaterialGraphCompileResult result = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0470U });
    Require(result.Succeeded(), "KBMAT-MAT47: vertex/instance data graph must compile");
    Require(result.shader.source.find("ctx.vertexColor") != std::string::npos, "KBMAT-MAT47: VertexColor must emit ctx.vertexColor");
    Require(result.shader.source.find("ctx.perInstanceRandom") != std::string::npos, "KBMAT-MAT47: PerInstanceRandom must emit ctx.perInstanceRandom");
    Require(result.shader.source.find("ctx.perInstanceFadeAmount") != std::string::npos, "KBMAT-MAT47: PerInstanceFadeAmount must emit ctx.perInstanceFadeAmount");
    Require(result.shader.source.find("material.roughness = ctx.perInstanceFadeAmount") != std::string::npos,
        "KBMAT-MAT47: DistanceCullFade must emit the same production distance-fade context value");
    Require(result.shader.source.find("ctx.perInstanceCustomData") != std::string::npos, "KBMAT-MAT47: PerInstanceCustomData must emit ctx.perInstanceCustomData");
    Require(result.shader.source.find("ctx.preSkinnedPosition") != std::string::npos, "KBMAT-MAT47: PreSkinnedPosition must emit ctx.preSkinnedPosition");
    Require(result.shader.source.find("ctx.preSkinnedNormal") != std::string::npos, "KBMAT-MAT47: PreSkinnedNormal must emit ctx.preSkinnedNormal");

    const auto hasVarying = [&result](std::string_view name) {
        return std::find(result.shader.reflection.requiredVaryings.begin(), result.shader.reflection.requiredVaryings.end(), std::string{ name }) !=
            result.shader.reflection.requiredVaryings.end();
    };
    Require(hasVarying("vertexColor"), "KBMAT-MAT47: VertexColor must declare a required vertexColor varying");
    Require(hasVarying("perInstanceRandom"), "KBMAT-MAT47: PerInstanceRandom must declare a required perInstanceRandom varying");
    Require(hasVarying("perInstanceFadeAmount"), "KBMAT-MAT47: PerInstanceFadeAmount must declare a required fade varying");
    Require(hasVarying("perInstanceCustomData0"), "KBMAT-MAT47: PerInstanceCustomData must declare a required custom-data varying");
    Require(hasVarying("preSkinnedPosition"), "KBMAT-MAT47: PreSkinnedPosition must declare a required pre-skinned position varying");
    Require(hasVarying("preSkinnedNormal"), "KBMAT-MAT47: PreSkinnedNormal must declare a required pre-skinned normal varying");
}

void RunMaterialGraphTimeAnimationNodeCodegenTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = "unlit";
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 2U, .kind = RenderMaterialGraphNodeKind::DeltaTime });
    graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = RenderMaterialGraphNodeKind::DynamicParameter });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::DynamicParameter, 3U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::DeltaTime, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));

    const RenderMaterialGraphCompileResult result = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0480U });
    Require(result.Succeeded(), "KBMAT-MAT30: time/animation node graph must compile");
    Require(result.shader.source.find("float deltaTime") != std::string::npos, "KBMAT-MAT30: generated context must declare deltaTime");
    Require(result.shader.source.find("vec4 dynamicParameter") != std::string::npos, "KBMAT-MAT30: generated context must declare dynamicParameter");
    Require(result.shader.source.find("ctx.deltaTime") != std::string::npos, "KBMAT-MAT30: DeltaTime must emit ctx.deltaTime");
    Require(result.shader.source.find("ctx.dynamicParameter") != std::string::npos, "KBMAT-MAT30: DynamicParameter must emit ctx.dynamicParameter");
    Require(result.shader.reflection.requiredVaryings.empty(), "KBMAT-MAT30: DeltaTime/DynamicParameter are uniforms and must not request vertex varyings");
}

void RunMaterialGraphSourceHashStabilityTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "tint", .displayName = "Tint", .defaultValueHint = "1 0 0 1" },
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult first = CompileRenderMaterialGraphToShaderSource(
        graph, RenderMaterialGraphBuildContext{ .assetId = 0x0211U, .sourcePath = "/Game/Materials/HashStability.kbmat" });
    const RenderMaterialGraphCompileResult second = CompileRenderMaterialGraphToShaderSource(
        graph, RenderMaterialGraphBuildContext{ .assetId = 0x0211U, .sourcePath = "/Game/Materials/HashStability.kbmat" });
    Require(first.Succeeded() && second.Succeeded(), "KBMAT-MAT02: Hash stability graph should compile twice");
    Require(first.shader.sourceHash != 0U, "KBMAT-MAT02: Source hash must be non-zero");
    Require(first.shader.sourceHash == second.shader.sourceHash,
        "KBMAT-MAT02: Same graph compiled twice must produce identical sourceHash");
    Require(first.shader.source == second.shader.source,
        "KBMAT-MAT02: Same graph compiled twice must produce identical source string");
}

void RunMaterialGraphTopologyChangeChangesHashTest() {
    RenderMaterialGraphDocument baseGraph = MakeDefaultRenderMaterialGraphDocument();
    baseGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "tint", .displayName = "Tint", .defaultValueHint = "1 1 1 1" },
    });
    baseGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    RenderMaterialGraphDocument extendedGraph = baseGraph;
    extendedGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = 120,
        .positionY = 180,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "metallic", .displayName = "Metallic", .defaultValueHint = "0" },
    });
    extendedGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "metallic"));

    const RenderMaterialGraphCompileResult baseResult = CompileRenderMaterialGraphToShaderSource(
        baseGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0212U });
    const RenderMaterialGraphCompileResult extResult = CompileRenderMaterialGraphToShaderSource(
        extendedGraph, RenderMaterialGraphBuildContext{ .assetId = 0x0212U });
    Require(baseResult.Succeeded() && extResult.Succeeded(), "KBMAT-MAT02: Topology change hash test graphs should compile");
    Require(baseResult.shader.sourceHash != extResult.shader.sourceHash,
        "KBMAT-MAT02: Adding a new parameter node must change the sourceHash");
}

void RunMaterialGraphDynamicParamDoesNotChangeHashTest() {
    RenderMaterialGraphDocument graphA = MakeDefaultRenderMaterialGraphDocument();
    graphA.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "roughness", .displayName = "Roughness", .defaultValueHint = "0.3" },
    });
    graphA.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));

    RenderMaterialGraphDocument graphB = graphA;
    graphB.nodes[1].parameter.defaultValueHint = "0.9";

    const RenderMaterialGraphCompileResult resultA = CompileRenderMaterialGraphToShaderSource(
        graphA, RenderMaterialGraphBuildContext{ .assetId = 0x0213U });
    const RenderMaterialGraphCompileResult resultB = CompileRenderMaterialGraphToShaderSource(
        graphB, RenderMaterialGraphBuildContext{ .assetId = 0x0213U });
    Require(resultA.Succeeded() && resultB.Succeeded(), "KBMAT-MAT02: Dynamic param hash test graphs should compile");
    Require(resultA.shader.sourceHash == resultB.shader.sourceHash,
        "KBMAT-MAT02: Changing ParameterScalar defaultValueHint must NOT change sourceHash");
    Require(resultA.shader.source == resultB.shader.source,
        "KBMAT-MAT02: Changing ParameterScalar defaultValueHint must NOT change shader source");
}

void RunMaterialGraphDeadCodeEliminationTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "activeColor", .displayName = "Active Color", .defaultValueHint = "1 1 1 1" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = 120,
        .positionY = 200,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "unusedParam", .displayName = "Unused Param", .defaultValueHint = "0.5" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 120,
        .positionY = 320,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "unusedTex",
            .displayName = "Unused Tex",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        },
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult result = CompileRenderMaterialGraphToShaderSource(
        graph, RenderMaterialGraphBuildContext{ .assetId = 0x0214U, .sourcePath = "/Game/Materials/DeadCode.kbmat" });
    Require(result.Succeeded(), "KBMAT-MAT02: Dead code elimination graph should compile");
    Require(result.shader.source.find("unusedParam") == std::string::npos,
        "KBMAT-MAT02: Unreachable ParameterScalar must not appear in compiled shader source");
    Require(result.shader.source.find("unusedTex") == std::string::npos,
        "KBMAT-MAT02: Unreachable TextureSample must not appear in compiled shader source");
    Require(result.shader.reflection.uniforms.size() == 1U,
        "KBMAT-MAT02: Reflection uniforms must only include reachable nodes");
    Require(result.shader.reflection.uniforms[0].stableId == "activeColor",
        "KBMAT-MAT02: Reflection uniform stableId must match the reachable ParameterColor node");
    Require(result.shader.reflection.textures.empty(),
        "KBMAT-MAT02: Reflection textures must be empty when TextureSample is unreachable");
}

[[nodiscard]] std::size_t CountSubstringOccurrences(const std::string& haystack, std::string_view needle) {
    std::size_t count = 0U;
    std::size_t position = haystack.find(needle, 0U);
    while (position != std::string::npos) {
        ++count;
        position = haystack.find(needle, position + needle.size());
    }
    return count;
}

void RunMaterialGraphTopologicalCseSharedNodeTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 120,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "shared",
            .displayName = "Shared Texture",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        },
    });
    // The single TextureSample node fans out to two Material Output pins.
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 2U, "r", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));

    const RenderMaterialGraphCompileResult result = CompileRenderMaterialGraphToShaderSource(
        graph, RenderMaterialGraphBuildContext{ .assetId = 0x0215U, .sourcePath = "/Game/Materials/SharedTextureCse.kbmat" });
    Require(result.Succeeded(), "KBMAT-MAT02: Shared TextureSample graph should compile");

    // Common-subexpression elimination: the shared node must be sampled exactly once.
    Require(CountSubstringOccurrences(result.shader.source, "texture2D(") == 1U,
        "KBMAT-MAT02: A TextureSample shared by multiple outputs must be sampled exactly once via a hoisted temporary");

    const std::size_t tempDecl = result.shader.source.find("vec4 n2_v = texture2D(");
    Require(tempDecl != std::string::npos,
        "KBMAT-MAT02: Shared node must be emitted as a topologically-ordered temporary");
    const std::size_t baseColorUse = result.shader.source.find("material.baseColor = n2_v;");
    Require(baseColorUse != std::string::npos,
        "KBMAT-MAT02: Material Output must reference the shared node temporary for baseColor");
    Require(result.shader.source.find("material.roughness = (n2_v).r;") != std::string::npos,
        "KBMAT-MAT02: Material Output must reference the shared node temporary channel for roughness");
    Require(tempDecl < baseColorUse,
        "KBMAT-MAT02: Topological ordering requires the temporary to be declared before it is used");

    const RenderMaterialGraphCompileResult repeat = CompileRenderMaterialGraphToShaderSource(
        graph, RenderMaterialGraphBuildContext{ .assetId = 0x0215U, .sourcePath = "/Game/Materials/SharedTextureCse.kbmat" });
    Require(repeat.shader.source == result.shader.source && repeat.shader.sourceHash == result.shader.sourceHash,
        "KBMAT-MAT02: Topologically-ordered codegen must be deterministic across compiles");
    Require(result.shader.reflection.textures.size() == 1U && result.shader.reflection.textures[0].stableId == "shared",
        "KBMAT-MAT02: Shared TextureSample must contribute exactly one reflection texture binding");
}

void RunMaterialGraphNodeSupportMatrixCoverageTest() {
    const std::span<const RenderMaterialGraphNodeKind> kinds = AllRenderMaterialGraphNodeKinds();
    Require(!kinds.empty(), "KBMAT-MAT03: Node support matrix must enumerate node kinds");
    const std::vector<RenderMaterialGraphNodeSupportMatrixEntry> matrix = BuildRenderMaterialGraphNodeSupportMatrix();
    Require(matrix.size() == kinds.size(), "MAT-71: BuildRenderMaterialGraphNodeSupportMatrix must expose one row per node kind");

    // Every enumerated node kind must carry an explicit, non-Unsupported authoring status.
    for (std::size_t index = 0U; index < kinds.size(); ++index) {
        const RenderMaterialGraphNodeKind kind = kinds[index];
        const RenderMaterialGraphNodeSupportMatrixEntry& entry = matrix[index];
        Require(entry.kind == kind, "MAT-71: Support matrix row order must match AllRenderMaterialGraphNodeKinds");
        Require(!entry.note.empty(), "MAT-71: Support matrix rows must carry a release-note rationale");
        Require(entry.authoringSupport == RenderMaterialGraphNodeSupportStatus(kind),
            "MAT-71: Support matrix authoring status must match the node support function");
        Require(entry.gpuForwardSupport == RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::GpuForward) &&
                entry.gpuShadowSupport == RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::GpuShadow) &&
                entry.gpuDeferredSupport == RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::GpuDeferred) &&
                entry.cpuFallbackSupport == RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::CpuFallback) &&
                entry.previewSupport == RenderMaterialGraphNodeSupportForPath(kind, RenderMaterialGraphRenderPath::Preview),
            "MAT-71: Support matrix path statuses must match RenderMaterialGraphNodeSupportForPath");
        Require(RenderMaterialGraphNodeSupportStatus(kind) != RenderMaterialGraphNodeSupport::Unsupported,
            "KBMAT-MAT03: Every enumerated node kind must have an explicit support matrix entry");
        const bool deferredSceneBindingNode =
            kind == RenderMaterialGraphNodeKind::SceneDepth ||
            kind == RenderMaterialGraphNodeKind::SceneColor ||
            kind == RenderMaterialGraphNodeKind::SceneTexture ||
            kind == RenderMaterialGraphNodeKind::DepthFade;
        Require(deferredSceneBindingNode
                ? entry.gpuDeferredSupport == RenderMaterialGraphNodeSupport::Unsupported
                : entry.gpuDeferredSupport == entry.authoringSupport,
            "MAT-71: Deferred/GBuffer graph support must match authoring support except scene texture/depth nodes that lack a deferred geometry binding");
    }

    // Cross-check: a node kind is in the canonical list iff it has a known (non-Unsupported) status.
    // This detects a node enum value that is missing from the support matrix.
    for (std::uint32_t raw = 0U; raw < 256U; ++raw) {
        const RenderMaterialGraphNodeKind kind = static_cast<RenderMaterialGraphNodeKind>(raw);
        const bool inList = std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
        const bool statusKnown = RenderMaterialGraphNodeSupportStatus(kind) != RenderMaterialGraphNodeSupport::Unsupported;
        Require(inList == statusKnown,
            "KBMAT-MAT03: Canonical node-kind list and support matrix must agree on every enum value");
    }

    // An out-of-range node kind must be reported Unsupported with an explicit UI tag.
    const RenderMaterialGraphNodeKind bogus = static_cast<RenderMaterialGraphNodeKind>(212U);
    Require(RenderMaterialGraphNodeSupportStatus(bogus) == RenderMaterialGraphNodeSupport::Unsupported,
        "KBMAT-MAT03: Unknown node kind must resolve to Unsupported");
    Require(RenderMaterialGraphNodeSupportShortTag(bogus) == std::string_view{ "UNSUPPORTED" },
        "KBMAT-MAT03: Unsupported node must expose an explicit UI tag");

    // Production nodes carry no UI tag (no decoration on the supported common case).
    Require(RenderMaterialGraphNodeSupportShortTag(RenderMaterialGraphNodeKind::Multiply).empty(),
        "KBMAT-MAT03: Production node must not be decorated with an unsupported/experimental UI tag");

    // The path dimension must be live: production render paths keep supported nodes Production.
    Require(RenderMaterialGraphNodeSupportForPath(RenderMaterialGraphNodeKind::Multiply, RenderMaterialGraphRenderPath::GpuForward) == RenderMaterialGraphNodeSupport::Production,
        "KBMAT-MAT03: Production node on the forward path must remain Production");
    Require(IsRenderMaterialGraphRenderPathProduction(RenderMaterialGraphRenderPath::GpuDeferred),
        "Deferred/GBuffer graph path must be reported as production after real GBuffer writer, deferred lighting and GPU readback coverage");
    Require(RenderMaterialGraphNodeSupportForPath(RenderMaterialGraphNodeKind::Multiply, RenderMaterialGraphRenderPath::GpuDeferred) == RenderMaterialGraphNodeSupport::Production,
        "Supported graph nodes on the Deferred path must remain Production");
    Require(RenderMaterialGraphNodeSupportForPath(RenderMaterialGraphNodeKind::SceneColor, RenderMaterialGraphRenderPath::GpuDeferred) == RenderMaterialGraphNodeSupport::Unsupported &&
            RenderMaterialGraphNodeSupportForPath(RenderMaterialGraphNodeKind::SceneDepth, RenderMaterialGraphRenderPath::GpuDeferred) == RenderMaterialGraphNodeSupport::Unsupported,
        "Scene color/depth graph nodes must fail closed on Deferred/GBuffer until explicit deferred bindings exist");
    Require(RenderMaterialGraphNodeSupportForPath(RenderMaterialGraphNodeKind::PixelDepth, RenderMaterialGraphRenderPath::GpuDeferred) == RenderMaterialGraphNodeSupport::Production,
        "PixelDepth must remain Production on Deferred because it reads the current fragment depth, not a scene texture binding");
    Require(RenderMaterialGraphNodeSupportForPath(bogus, RenderMaterialGraphRenderPath::GpuForward) == RenderMaterialGraphNodeSupport::Unsupported,
        "KBMAT-MAT03: Unsupported node stays Unsupported on every render path");

    const std::filesystem::path docsPath = FindRepositoryFile("docs/material_graph.md");
    Require(!docsPath.empty(), "MAT-71: Material Graph developer/artist documentation must exist");
    const std::string docs = ReadRequiredTextFile(docsPath);
    Require(docs.find("MAT-71") != std::string::npos &&
            docs.find("RenderMaterialGraphNodeSupport") != std::string::npos &&
            docs.find("kb_standalone_player") != std::string::npos &&
            docs.find("GpuDeferred` | Production") != std::string::npos &&
            docs.find("Forward+ / clustered lighting | Production") != std::string::npos &&
            docs.find("Apple/Metal | Production shader profile") != std::string::npos &&
            docs.find("native Metal GPU readback remains platform-specific") != std::string::npos,
        "MAT-71: Material Graph documentation must include support matrix, standalone runtime, render-path gating and Metal status");

    const std::filesystem::path gatePath = FindRepositoryFile("tests/run-material-graph-release-gate.ps1");
    Require(!gatePath.empty(), "MAT-71: Material Graph release gate script must exist");
    const std::string gate = ReadRequiredTextFile(gatePath);
    Require(gate.find("kb_renderer_tests") != std::string::npos &&
            gate.find("kb_editor_tests") != std::string::npos &&
            gate.find("kb_editor") != std::string::npos &&
            gate.find("kb_standalone_player") != std::string::npos &&
            gate.find("--self-test") != std::string::npos &&
            gate.find("GraphForwardGpuRenderTests") != std::string::npos &&
            gate.find("run-render-smoke.ps1") != std::string::npos &&
            gate.find("Assert-NoMaterialGraphPlaceholderTokens") != std::string::npos,
        "MAT-71: release gate must build/test renderer, editor, standalone runtime, visual smoke and no-placeholder checks");
}

void RunMaterialGraphUnsupportedNodeValidationTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::Multiply,
        .positionX = 200,
        .positionY = 120,
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 60,
        .positionY = 120,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "1 1 1 1" },
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::Multiply, 2U, "a"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Multiply, 2U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    // Forward (production) path: no render-path support diagnostics.
    const std::vector<RenderMaterialGraphDiagnostic> forwardDiagnostics =
        ValidateRenderMaterialGraphDocument(graph, RenderMaterialGraphRenderPath::GpuForward);
    Require(!HasGraphDiagnostic(forwardDiagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode),
        "KBMAT-MAT03: Production nodes on the forward path must not raise render-path support diagnostics");

    // Deferred/GBuffer is production after the real graph GBuffer writer, deferred lighting and readback proof.
    const std::vector<RenderMaterialGraphDiagnostic> deferredDiagnostics =
        ValidateRenderMaterialGraphDocument(graph, RenderMaterialGraphRenderPath::GpuDeferred);
    const RenderMaterialGraphDiagnostic* pathDiagnostic = FindGraphDiagnostic(deferredDiagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode);
    Require(pathDiagnostic == nullptr,
        "Deferred production path must not raise a render-path support diagnostic for supported graph nodes");

    RenderMaterialGraphDocument deferredOpaqueSceneDepthGraph = MakeDefaultRenderMaterialGraphDocument();
    deferredOpaqueSceneDepthGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 4U, .kind = RenderMaterialGraphNodeKind::SceneDepth });
    deferredOpaqueSceneDepthGraph.nodes.push_back(RenderMaterialGraphNode{ .id = 5U, .kind = RenderMaterialGraphNodeKind::MakeVector });
    deferredOpaqueSceneDepthGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::SceneDepth, 4U, "value", RenderMaterialGraphNodeKind::MakeVector, 5U, "x"));
    deferredOpaqueSceneDepthGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::SceneDepth, 4U, "value", RenderMaterialGraphNodeKind::MakeVector, 5U, "y"));
    deferredOpaqueSceneDepthGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::SceneDepth, 4U, "value", RenderMaterialGraphNodeKind::MakeVector, 5U, "z"));
    deferredOpaqueSceneDepthGraph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::MakeVector, 5U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const std::vector<RenderMaterialGraphDiagnostic> deferredOpaqueSceneDepthDiagnostics =
        ValidateRenderMaterialGraphDocument(deferredOpaqueSceneDepthGraph, RenderMaterialGraphRenderPath::GpuDeferred);
    Require(HasGraphDiagnostic(deferredOpaqueSceneDepthDiagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode),
        "Deferred opaque/GBuffer graph must still reject scene-depth sampling because the geometry pass has no scene-depth binding");

    RenderMaterialGraphDocument deferredTransparentSceneDepthGraph = deferredOpaqueSceneDepthGraph;
    deferredTransparentSceneDepthGraph.blendMode = "translucent";
    const std::vector<RenderMaterialGraphDiagnostic> deferredTransparentSceneDepthDiagnostics =
        ValidateRenderMaterialGraphDocument(deferredTransparentSceneDepthGraph, RenderMaterialGraphRenderPath::GpuDeferred);
    Require(!HasGraphDiagnostic(deferredTransparentSceneDepthDiagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedRenderPathNode),
        "Deferred transparent graph must accept SceneDepth because BaseTransparent binds the GBuffer depth texture");

    // Unknown node kind must still be rejected as an unsupported node.
    RenderMaterialGraphDocument unknownGraph = MakeDefaultRenderMaterialGraphDocument();
    unknownGraph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = static_cast<RenderMaterialGraphNodeKind>(231U),
        .positionX = 100,
        .positionY = 100,
    });
    const std::vector<RenderMaterialGraphDiagnostic> unknownDiagnostics = ValidateRenderMaterialGraphDocument(unknownGraph);
    Require(HasGraphDiagnostic(unknownDiagnostics, RenderMaterialGraphDiagnosticKind::UnsupportedNode),
        "KBMAT-MAT03: Unknown node kind must produce an unsupported-node diagnostic");
}

void RunMaterialGraphProgramBindingResourceTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = 80,
        .positionY = 60,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "tint", .displayName = "Tint", .defaultValueHint = "1 1 1 1" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ParameterScalar,
        .positionX = 80,
        .positionY = 160,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "rough", .displayName = "Rough", .defaultValueHint = "0.5" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 80,
        .positionY = 260,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "albedoTex",
            .displayName = "Albedo",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        },
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 4U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterScalar, 3U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "roughness"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0601U });
    Require(compiled.Succeeded(), "KBMAT-MAT06: Graph must compile before building program binding");

    const std::array<RenderMaterialGraphParameterValue, 3U> values{
        RenderMaterialGraphParameterValue{ .stableId = "tint", .type = RenderMaterialParameterType::Color, .numbers = { 0.2F, 0.4F, 0.6F, 1.0F } },
        RenderMaterialGraphParameterValue{ .stableId = "rough", .type = RenderMaterialParameterType::Scalar, .numbers = { 0.25F, 0.0F, 0.0F, 0.0F } },
        RenderMaterialGraphParameterValue{ .stableId = "albedoTex", .type = RenderMaterialParameterType::Texture, .assetId = 0xAAA1U },
    };
    const RenderMaterialGraphProgramBindingResult result = BuildRenderMaterialGraphProgramBinding(0x9911U, 2U, compiled.shader, values);

    Require(result.binding.active, "KBMAT-MAT06: Graph material binding must be active");
    Require(result.binding.graphSourceHash == compiled.shader.sourceHash, "KBMAT-MAT06: Binding must carry the graph program key (graph source hash)");
    Require(result.binding.variantKey == RenderMaterialGraphVariantKey(compiled.shader) &&
            result.binding.pipelineStateKey == RenderMaterialGraphPipelineStateKey(compiled.shader),
        "KBMAT-MAT66: Binding must carry explicit graph variant and pipeline-state keys");
    Require(result.binding.materialTypeId == 0x9911U && result.binding.materialTypeVersion == 2U, "KBMAT-MAT06: Binding must carry the material type id/version program key");
    Require(result.binding.uniforms.size() == 2U, "KBMAT-MAT06: Binding must expose one dynamic uniform per reachable parameter");
    Require(result.binding.textures.size() == 1U, "KBMAT-MAT06: Binding must expose one texture/sampler binding per TextureSample node");
    Require(result.binding.textures[0].stableId == "albedoTex" && result.binding.textures[0].colorSpace == RenderTextureColorSpace::Srgb,
        "KBMAT-MAT06: Texture binding must carry stable id and sRGB color space from reflection");
    Require(result.binding.textures[0].textureAssetId == 0xAAA1U && result.binding.textures[0].resolved,
        "KBMAT-MAT06: Texture binding must resolve the bound texture asset id");
    Require(!result.binding.requiredVaryings.empty() && result.binding.requiredVaryings[0] == "uv0",
        "KBMAT-MAT06: Binding must carry the required varyings from reflection");

    const RenderMaterialGraphUniformBinding* tint = nullptr;
    for (const RenderMaterialGraphUniformBinding& uniform : result.binding.uniforms) {
        if (uniform.stableId == "tint") {
            tint = &uniform;
        }
    }
    Require(tint != nullptr && tint->type == RenderMaterialGraphUniformBindingType::Color && NearlyEqual(tint->value[0], 0.2F) && NearlyEqual(tint->value[2], 0.6F),
        "KBMAT-MAT06: Dynamic uniform binding must capture the parameter value and type");

    // Builtin PBR materials must keep working without graph program fields.
    RenderResourceRegistry registry;
    const RenderMaterialHandle builtin = registry.RegisterMaterial(RenderMaterialDesc{});
    Require(builtin.IsValid() && registry.FindMaterial(builtin) != nullptr && !registry.FindMaterial(builtin)->graphProgram.active,
        "KBMAT-MAT06: Builtin PBR material resource must remain valid with an inactive graph program binding");

    // Graph material resource carries the program key and binding layout once registered.
    const RenderMaterialHandle graphMaterial = registry.RegisterMaterial(RenderMaterialDesc{}, result.binding);
    const RenderMaterialResource* graphResource = registry.FindMaterial(graphMaterial);
    Require(graphResource != nullptr && graphResource->graphProgram.active &&
            graphResource->graphProgram.graphSourceHash == compiled.shader.sourceHash &&
            graphResource->graphProgram.textures.size() == 1U,
        "KBMAT-MAT06: Registered graph material resource must own the program key and binding layout");
}

void RunMaterialGraphProgramBindingDynamicOverrideTest() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ParameterColor,
        .positionX = 80,
        .positionY = 60,
        .parameter = RenderMaterialGraphParameterMetadata{ .stableId = "tint", .displayName = "Tint", .defaultValueHint = "1 1 1 1" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::TextureSample,
        .positionX = 80,
        .positionY = 160,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = "albedoTex",
            .displayName = "Albedo",
            .textureRole = "baseColor",
            .expectedTextureColorSpace = RenderMaterialTextureColorSpace::Srgb,
            .overrideSupported = true,
        },
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ParameterColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureSample, 3U, "color", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "emissive"));

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0602U });
    Require(compiled.Succeeded(), "KBMAT-MAT06: Override graph must compile");

    // No bound texture asset -> missing-binding diagnostic + fallback (unresolved) binding.
    const std::array<RenderMaterialGraphParameterValue, 1U> baseValues{
        RenderMaterialGraphParameterValue{ .stableId = "tint", .type = RenderMaterialParameterType::Color, .numbers = { 1.0F, 1.0F, 1.0F, 1.0F } },
    };
    const RenderMaterialGraphProgramBindingResult missing = BuildRenderMaterialGraphProgramBinding(0x77U, 1U, compiled.shader, baseValues);
    Require(HasGraphDiagnostic(missing.diagnostics, RenderMaterialGraphDiagnosticKind::MissingTexture),
        "KBMAT-MAT06: A missing texture binding must produce a diagnostic");
    Require(missing.binding.textures.size() == 1U && !missing.binding.textures[0].resolved,
        "KBMAT-MAT06: A missing texture binding must remain unresolved for the runtime fallback");

    // A dynamic override is only a change of parameter VALUES (not topology); it must keep the same
    // graph program key while updating the uniform/texture bindings, with no shader recompilation.
    const std::array<RenderMaterialGraphParameterValue, 2U> overridden{
        RenderMaterialGraphParameterValue{ .stableId = "tint", .type = RenderMaterialParameterType::Color, .numbers = { 0.1F, 0.2F, 0.3F, 1.0F } },
        RenderMaterialGraphParameterValue{ .stableId = "albedoTex", .type = RenderMaterialParameterType::Texture, .assetId = 0xBBB2U },
    };
    const std::uint64_t compileInvocationsBefore = RenderMaterialGraphCompileInvocationCount();
    const RenderMaterialGraphProgramBindingResult after = BuildRenderMaterialGraphProgramBinding(0x77U, 1U, compiled.shader, overridden);
    Require(RenderMaterialGraphCompileInvocationCount() == compileInvocationsBefore,
        "KBMAT-MAT06: Re-binding dynamic parameter values must not recompile the graph shader");

    Require(after.binding.graphSourceHash == missing.binding.graphSourceHash &&
            after.binding.materialTypeId == missing.binding.materialTypeId &&
            after.binding.materialTypeVersion == missing.binding.materialTypeVersion,
        "KBMAT-MAT06: Dynamic override must NOT change the graph program key");
    Require(NearlyEqual(after.binding.uniforms[0].value[0], 0.1F) && NearlyEqual(after.binding.uniforms[0].value[2], 0.3F),
        "KBMAT-MAT06: Dynamic override must update the uniform binding value");
    Require(after.binding.textures[0].textureAssetId == 0xBBB2U && after.binding.textures[0].resolved,
        "KBMAT-MAT06: Dynamic override must update the texture binding without recompiling the shader");
}

void RunMaterialGraphRuntimeStateTransitionsTest() {
    using State = RenderMaterialGraphRuntimeState;
    using Phase = RenderMaterialGraphCompilePhase;

    Require(ResolveRenderMaterialGraphRuntimeState(RenderMaterialGraphRuntimeStateInput{ .phase = Phase::Editing }) == State::Dirty,
        "KBMAT-MAT14: An edited graph must report Dirty");
    Require(ResolveRenderMaterialGraphRuntimeState(RenderMaterialGraphRuntimeStateInput{ .phase = Phase::Validating }) == State::Validating,
        "KBMAT-MAT14: A validating graph must report Validating");
    Require(ResolveRenderMaterialGraphRuntimeState(RenderMaterialGraphRuntimeStateInput{ .phase = Phase::Compiling }) == State::Compiling,
        "KBMAT-MAT14: A compiling graph must report Compiling");

    Require(ResolveRenderMaterialGraphRuntimeState(RenderMaterialGraphRuntimeStateInput{
        .phase = Phase::Compiled, .validationSucceeded = true, .compileSucceeded = true, .hasGpuProgram = true }) == State::UsingGpuGraph,
        "KBMAT-MAT14: A successfully compiled graph with a GPU program must report UsingGpuGraph");

    Require(ResolveRenderMaterialGraphRuntimeState(RenderMaterialGraphRuntimeStateInput{
        .phase = Phase::Compiled, .compileSucceeded = false, .fallbackApplied = false }) == State::CompileFailed,
        "KBMAT-MAT14: A failed compile before fallback resolution must report CompileFailed");

    Require(ResolveRenderMaterialGraphRuntimeState(RenderMaterialGraphRuntimeStateInput{
        .phase = Phase::Compiled, .compileSucceeded = false, .hasLastGood = true, .fallbackApplied = true,
        .failurePolicy = RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial }) == State::UsingLastGood,
        "KBMAT-MAT14: A failed compile with a last-good artifact must keep the last-good program active");

    Require(ResolveRenderMaterialGraphRuntimeState(RenderMaterialGraphRuntimeStateInput{
        .phase = Phase::Compiled, .compileSucceeded = false, .hasLastGood = false, .fallbackApplied = true }) == State::UsingErrorMaterial,
        "KBMAT-MAT14: A failed compile without a last-good artifact must fall back to the error material");

    Require(ResolveRenderMaterialGraphRuntimeState(RenderMaterialGraphRuntimeStateInput{
        .phase = Phase::Compiled, .compileSucceeded = false, .hasLastGood = true, .fallbackApplied = true,
        .failurePolicy = RenderMaterialGraphArtifactFailurePolicy::ErrorMaterial }) == State::UsingErrorMaterial,
        "KBMAT-MAT14: An explicit error-material policy must skip the last-good artifact");

    Require(RenderMaterialGraphRuntimeStateName(State::UsingGpuGraph) == std::string_view{ "UsingGpuGraph" },
        "KBMAT-MAT14: Runtime state names must be stable");
    Require(RenderMaterialGraphRuntimeStateUsesFallback(State::UsingLastGood) && RenderMaterialGraphRuntimeStateUsesFallback(State::UsingErrorMaterial),
        "KBMAT-MAT14: Last-good and error-material states must be reported as fallback");
    Require(!RenderMaterialGraphRuntimeStateUsesFallback(State::UsingGpuGraph),
        "KBMAT-MAT14: The GPU graph state must not be reported as fallback");
}

void RunMaterialGraphProductionNodeCompileTest() {
    Require(RenderMaterialGraphNodeSupportStatus(RenderMaterialGraphNodeKind::Multiply) == RenderMaterialGraphNodeSupport::Production,
        "KBMAT-MAT03: Multiply must be declared a production node");

    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 60,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.5 0.25 0.75 1" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 60,
        .positionY = 180,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "2 2 2 1" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 4U,
        .kind = RenderMaterialGraphNodeKind::Multiply,
        .positionX = 240,
        .positionY = 120,
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::Multiply, 4U, "a"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 3U, "rgba", RenderMaterialGraphNodeKind::Multiply, 4U, "b"));
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::Multiply, 4U, "value", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));

    const RenderMaterialGraphCompileResult result = CompileRenderMaterialGraphToShaderSource(
        graph, RenderMaterialGraphBuildContext{ .assetId = 0x0301U, .sourcePath = "/Game/Materials/ProductionNode.kbmat" });
    Require(result.Succeeded(), "KBMAT-MAT03: Production node graph must compile to shader source");
    Require(result.shader.source.find(" * ") != std::string::npos,
        "KBMAT-MAT03: Production Multiply node must emit a GPU multiply expression");
}

} // namespace

void RunRenderMaterialTypeSchemaTests() {
    RunBuiltInPbrMaterialTypeSchemaExistsTest();
    RunBuiltInPbrMaterialTypeDocumentExistsTest();
    RunMaterialVersioningContractsTest();
    RunMaterialTypeDocumentRoundTripTest();
    RunBuiltInPbrSchemaCoversAllMaterialFieldsTest();
    RunBuiltInPbrSchemaHasCorrectRangesTest();
    RunBuiltInPbrSchemaTextureSlotsHaveColorSpaceTest();
    RunKbmat1009MaterialTextureSlotColorSpaceRuntimeGateTest();
    RunKbmat0602To0605PbrTextureSchemaTest();
    RunKbmat0602To0605MaterialBindingRuntimeTest();
    RunKbmat0606OpaqueAlphaRuntimeTest();
    RunKbmat0607AlphaMaskCutoffRuntimeTest();
    RunKbmat0608AlphaBlendSchemaReasonTest();
    RunKbmat0609DoubleSidedSchemaTest();
    RunKbmat0609DoubleSidedRenderStateRuntimeTest();
    RunMaterialPassPolicyAppliesMaterialRenderStateTest();
    RunMaterialTranslucencyBlendRoundTripTest();
    RunBuiltInPbrSchemaDistinguishesSupportedVsAdvancedTest();
    RunBuiltInPbrSchemaParserUsesSchemaForValidationTest();
    RunBuiltInPbrSchemaParserUsesSchemaForUnsupportedFieldsTest();
    RunBuiltInPbrSchemaParserWarnsForEveryIgnoredAdvancedFieldTest();
    RunMaterialAssetAtomicSaveAndRoundTripTest();
#if defined(_WIN32)
    RunMaterialAtomicSaveFailurePreservesPreviousVersionTest();
#endif
    RunMaterialInstanceOverrideRoundTripAndValidatorTest();
    RunMaterialInstanceStaticAndBaseOverrideTest();
    RunBuiltInPbrSchemaParserReportsTextureColorSpaceDiagnosticsTest();
    RunBuiltInPbrSchemaParserTextureColorSpaceDiagnosticsOnlyForAssignedTexturesTest();
    RunBuiltInPbrSchemaParserTextureColorSpaceDiagnosticsForPathFieldsTest();
    RunBuiltInPbrSchemaAlphaModesListedTest();
    RunBuiltInPbrSchemaUnsupportedAdvancedFeaturesListedTest();
    RunMaterialTypeMigrationTableAppliesLegacyFieldsTest();
    RunMaterialAssetTilingOffsetRoundTripTest();
    RunMaterialGraphRoundTripTest();
    RunMaterialGraphSchemaMigrationGoldenTest();
    RunMaterialGraphStableLinkIdMigrationTest();
    RunMaterialGraphMultiWordNodeKindSerializationRoundTripTest();
    RunMaterialGraphEveryShaderNodeKindHasCodegenTest();
    RunMaterialGraphDefaultsLegacyMaterialToOutputNodeTest();
    RunMaterialGraphLastGoodArtifactPolicyRoundTripAndDecisionTest();
    RunMaterialGraphMvpNodeKindsAndPinsTest();
    RunMaterialGraphRejectsPartialLastGoodArtifactTest();
    RunMaterialGraphRejectsInvalidLinksTest();
    RunMaterialGraphTypedPinCompatibilityTest();
    RunMaterialGraphParameterNodesGenerateMaterialTypeSchemaTest();
    RunMaterialGraphRejectsInvalidParameterMetadataTest();
    RunMaterialGraphRuntimeDiagnosticsTest();
    RunMaterialGraphTypedIrBuildTest();
    RunMaterialGraphShaderSourceCompilerMvpTest();
    RunMaterialGraphOrganizationNodeCodegenTest();
    RunMaterialGraphFunctionInliningTest();
    RunMaterialGraphLayerStackInliningTest();
    RunMaterialGraphMaterialTypeDocumentGenerationTest();
    RunMaterialGraphCompileArtifactCacheTest();
    RunMaterialGraphCompilerDiagnosticsCoverageTest();
    RunMaterialParserDiagnosticsCarrySourceContextTest();
    RunMaterialGraphSurfaceContractDefaultsTest();
    RunMaterialGraphContextContractDefaultsTest();
    RunMaterialGraphAlphaClipThresholdPinTest();
    RunMaterialGraphOutputPinTypeMismatchDiagnosticTest();
    RunMaterialGraphReflectionUniformAndTextureCountTest();
    RunMaterialGraphNormalMapTextureRoleInferenceTest();
    RunMaterialParameterCollectionAssetRoundTripTest();
    RunMaterialGraphCollectionParameterBindingTest();
    RunMaterialGraphTextureExpansionSchemaTest();
    RunMaterialGraphCustomCodeNodeSchemaTest();
    RunMaterialGraphVertexDataReflectionTest();
    RunMaterialGraphTimeAnimationNodeCodegenTest();
    RunMaterialGraphTextureSamplerLimitTest();
    RunMaterialOutputPinsCodegenTest();
    RunMaterialCustomOutputsSchemaAndCodegenTest();
    RunMaterialDomainGatingTest();
    RunMaterialShadingModelGatingTest();
    RunMaterialGraphBlendModeTest();
    RunMaterialGraphBlendSceneStateTest();
    RunMaterialStaticPermutationTest();
    RunMaterialGraphProgramBindingIdentityKeyTest();
    RunMaterialVertexDomainOutputCodegenTest();
    RunMaterialVariantSwitchPermutationTest();
    RunMaterialCoordinateNodeCodegenTest();
    RunMaterialWorldSpaceNodeCodegenTest();
    RunMaterialGraphSourceHashStabilityTest();
    RunMaterialGraphTopologyChangeChangesHashTest();
    RunMaterialGraphDynamicParamDoesNotChangeHashTest();
    RunMaterialGraphDeadCodeEliminationTest();
    RunMaterialGraphTopologicalCseSharedNodeTest();
    RunMaterialGraphNodeSupportMatrixCoverageTest();
    RunMaterialGraphUnsupportedNodeValidationTest();
    RunMaterialGraphProductionNodeCompileTest();
    RunMaterialGraphProgramBindingResourceTest();
    RunMaterialGraphProgramBindingDynamicOverrideTest();
    RunMaterialGraphRuntimeStateTransitionsTest();
}

} // namespace kb::render::tests
