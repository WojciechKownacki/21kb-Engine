#include "RendererTestSupport.hpp"

#include "kb/render/resources/RenderMaterialAssetWriter.hpp"
#include "kb/render/resources/RenderMaterialTypeSchema.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "../src/scene/pipeline/MeshPipelinePassPolicy.hpp"
#include "../src/scene/submit/SceneMeshMaterialBindingResolver.hpp"

#include <filesystem>
#include <sstream>
#include <string>

namespace kb::render::tests {
namespace {

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
    Require(result.asset.has_value(), "Asset should still be parsed with unsupported fields");
    Require(!result.Succeeded(), "Should have warnings");
    Require(result.diagnostics.size() == 2U, "Should report 2 unsupported advanced field diagnostics");
    Require(result.diagnostics[0].code == RenderMaterialAssetParseDiagnosticCode::UnsupportedAdvancedField, "First diagnostic should be UnsupportedAdvancedField");
    Require(result.diagnostics[0].field == "clearcoatFactor", "First diagnostic should be for clearcoatFactor");
    Require(result.diagnostics[1].field == "transmissionTexture", "Second diagnostic should be for transmissionTexture");
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
    });
    original.graph.links.push_back(RenderMaterialGraphLink{
        .fromNodeId = 2U,
        .fromPin = "rgba",
        .toNodeId = 1U,
        .toPin = "baseColor",
    });

    std::ostringstream output;
    RenderMaterialAssetWriter::Write(output, original);
    Require(output.str().find("graphVersion 1\n") != std::string::npos, "Material writer did not emit graph version");
    Require(output.str().find("graphNode 1 MaterialOutput 640 240\n") != std::string::npos, "Material writer did not emit material output node");
    Require(output.str().find("graphNode 2 ConstantColor 240 180\n") != std::string::npos, "Material writer did not emit constant color node");
    Require(output.str().find("graphLink 2 rgba 1 baseColor\n") != std::string::npos, "Material writer did not emit graph link");

    std::istringstream input{ output.str() };
    const RenderMaterialAssetParseResult result = RenderMaterialAssetLoader::LoadMaterialWithDiagnostics(input);
    Require(result.asset.has_value(), "Material graph round-trip should parse");
    Require(result.Succeeded(), "Material graph round-trip should have no diagnostics");
    Require(result.asset->graph.hasExplicitDocumentVersion, "Material graph round-trip lost explicit graph version");
    Require(result.asset->graph.nodes.size() == 2U, "Material graph round-trip lost nodes");
    Require(result.asset->graph.links.size() == 1U, "Material graph round-trip lost links");
    Require(result.asset->graph.nodes[0].kind == RenderMaterialGraphNodeKind::MaterialOutput, "Material graph round-trip changed output node kind");
    Require(result.asset->graph.nodes[1].kind == RenderMaterialGraphNodeKind::ConstantColor, "Material graph round-trip changed constant node kind");
    Require(result.asset->graph.links[0].fromNodeId == 2U && result.asset->graph.links[0].toNodeId == 1U, "Material graph round-trip changed link nodes");
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

} // namespace

void RunRenderMaterialTypeSchemaTests() {
    RunBuiltInPbrMaterialTypeSchemaExistsTest();
    RunBuiltInPbrSchemaCoversAllMaterialFieldsTest();
    RunBuiltInPbrSchemaHasCorrectRangesTest();
    RunBuiltInPbrSchemaTextureSlotsHaveColorSpaceTest();
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
    RunMaterialAssetAtomicSaveAndRoundTripTest();
    RunBuiltInPbrSchemaParserReportsTextureColorSpaceDiagnosticsTest();
    RunBuiltInPbrSchemaParserTextureColorSpaceDiagnosticsOnlyForAssignedTexturesTest();
    RunBuiltInPbrSchemaParserTextureColorSpaceDiagnosticsForPathFieldsTest();
    RunBuiltInPbrSchemaAlphaModesListedTest();
    RunBuiltInPbrSchemaUnsupportedAdvancedFeaturesListedTest();
    RunMaterialAssetTilingOffsetRoundTripTest();
    RunMaterialGraphRoundTripTest();
    RunMaterialGraphDefaultsLegacyMaterialToOutputNodeTest();
    RunMaterialGraphRejectsInvalidLinksTest();
}

} // namespace kb::render::tests
