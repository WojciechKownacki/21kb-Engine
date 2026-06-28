#include "RendererTestSupport.hpp"

#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "kb/render/resources/RenderMaterialTextureSlots.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetWriter.hpp"
#include "../src/scene/pipeline/MeshPipelinePassPolicy.hpp"
#include "../src/scene/submit/SceneMeshMaterialBindingResolver.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
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
    Require(document.renderPasses.size() == 5U, "KBMAT-GRAPH-0002: Built-in PBR material type should declare render pass support");
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
    Require(hasPass("ShadowDepth", RenderMaterialFeatureSupport::Supported), "KBMAT-GRAPH-0002: Built-in PBR material type missing ShadowDepth pass");
    Require(hasPass("SelectionId", RenderMaterialFeatureSupport::Supported), "KBMAT-GRAPH-0002: Built-in PBR material type missing SelectionId pass");
    Require(hasPass("BaseTransparent", RenderMaterialFeatureSupport::ParsedButIgnored), "KBMAT-GRAPH-0002: Built-in PBR material type should declare disabled transparent pass explicitly");

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
    Require(alphaMode->description.find("BLEND is parsed but disabled until the transparent pass is ready") != std::string_view::npos,
            "KBMAT-0608: alphaMode schema must document why BLEND is disabled");
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

    std::ostringstream output;
    RenderMaterialAssetWriter::Write(output, original);
    Require(output.str().find("graphVersion 1\n") != std::string::npos, "Material writer did not emit graph version");
    Require(output.str().find("graphMaterialDomain surface\n") != std::string::npos, "Material writer did not emit graph material domain");
    Require(output.str().find("graphShadingModel lit\n") != std::string::npos, "Material writer did not emit graph shading model");
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

    std::istringstream input{ output.str() };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Material graph round-trip should parse");
    Require(result.Succeeded(), "Material graph round-trip should have no diagnostics");
    Require(result.asset->graph.hasExplicitDocumentVersion, "Material graph round-trip lost explicit graph version");
    Require(result.asset->graph.materialDomain == "surface", "Material graph round-trip lost graph material domain");
    Require(result.asset->graph.shadingModel == "lit", "Material graph round-trip lost graph shading model");
    Require(result.asset->graph.storageModel == "inline-kbmat", "Material graph round-trip lost inline storage decision");
    Require(result.asset->graph.diagnosticSchemaVersion == 1U && result.asset->graph.persistCompileDiagnostics, "Material graph round-trip lost diagnostic metadata");
    Require(result.asset->graph.hasExplicitArtifactFailurePolicy, "Material graph round-trip lost explicit artifact failure policy");
    Require(result.asset->graph.artifactFailurePolicy == RenderMaterialGraphArtifactFailurePolicy::LastGoodThenErrorMaterial, "Material graph round-trip changed artifact failure policy");
    Require(result.asset->graph.nodes.size() == 2U, "Material graph round-trip lost nodes");
    Require(result.asset->graph.links.size() == 1U, "Material graph round-trip lost links");
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
    Require(ParseRenderMaterialGraphNodeKind("Vector") == RenderMaterialGraphNodeKind::ConstantVector, "Material graph MVP should parse Vector alias");
    Require(ParseRenderMaterialGraphNodeKind("Color") == RenderMaterialGraphNodeKind::ConstantColor, "Material graph MVP should parse Color alias");
    Require(ParseRenderMaterialGraphNodeKind("TextureCoordinate") == RenderMaterialGraphNodeKind::Uv, "Material graph MVP should parse TextureCoordinate as UV node");

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
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::Uv, "uv", true) == RenderMaterialGraphPinType::Float2, "KBMAT-GRAPH-0103: UV output should be float2");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ConstantVector, "xyz", true) == RenderMaterialGraphPinType::Float3, "KBMAT-GRAPH-0103: Vector output should be float3");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ConstantColor, "rgba", true) == RenderMaterialGraphPinType::Color, "KBMAT-GRAPH-0103: Color output should be color");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::ParameterTexture, "texture", true) == RenderMaterialGraphPinType::Texture2D, "KBMAT-GRAPH-0103: Texture parameter output should be texture2D");
    Require(RenderMaterialGraphPinDataType(RenderMaterialGraphNodeKind::NormalUnpack, "normal", true) == RenderMaterialGraphPinType::Normal, "KBMAT-GRAPH-0103: NormalUnpack output should be normal");
    Require(RenderMaterialGraphPinTypeName(RenderMaterialGraphPinType::Sampler) == "sampler", "KBMAT-GRAPH-0103: Pin type enum should expose sampler");
    Require(RenderMaterialGraphPinTypeName(RenderMaterialGraphPinType::Bool) == "bool", "KBMAT-GRAPH-0103: Pin type enum should expose bool");
    Require(AreRenderMaterialGraphPinsCompatible(RenderMaterialGraphPinType::Color, RenderMaterialGraphPinType::Float4), "KBMAT-GRAPH-0103: Color should feed float4 operator inputs");
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
        "KBMAT-GRAPH-0106: Disconnected Material Output BaseColor should use runtime black fallback without a graph error");
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
    Require(result.shader.source.find("GraphMaterial EvaluateMaterialGraph()") != std::string::npos,
        "KBMAT-GRAPH-0202: Shader compiler should generate a material graph entry function");
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
    RunBuiltInPbrSchemaDistinguishesSupportedVsAdvancedTest();
    RunBuiltInPbrSchemaParserUsesSchemaForValidationTest();
    RunBuiltInPbrSchemaParserUsesSchemaForUnsupportedFieldsTest();
    RunBuiltInPbrSchemaParserWarnsForEveryIgnoredAdvancedFieldTest();
    RunMaterialAssetAtomicSaveAndRoundTripTest();
    RunMaterialInstanceOverrideRoundTripAndValidatorTest();
    RunBuiltInPbrSchemaParserReportsTextureColorSpaceDiagnosticsTest();
    RunBuiltInPbrSchemaParserTextureColorSpaceDiagnosticsOnlyForAssignedTexturesTest();
    RunBuiltInPbrSchemaParserTextureColorSpaceDiagnosticsForPathFieldsTest();
    RunBuiltInPbrSchemaAlphaModesListedTest();
    RunBuiltInPbrSchemaUnsupportedAdvancedFeaturesListedTest();
    RunMaterialTypeMigrationTableAppliesLegacyFieldsTest();
    RunMaterialAssetTilingOffsetRoundTripTest();
    RunMaterialGraphRoundTripTest();
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
    RunMaterialGraphMaterialTypeDocumentGenerationTest();
    RunMaterialGraphCompileArtifactCacheTest();
    RunMaterialGraphCompilerDiagnosticsCoverageTest();
    RunMaterialParserDiagnosticsCarrySourceContextTest();
}

} // namespace kb::render::tests
