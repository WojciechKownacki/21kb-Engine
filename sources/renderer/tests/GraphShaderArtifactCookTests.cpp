#include "RendererTestSupport.hpp"

#include "engine/platform/FileSystemPath.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"

#include <algorithm>
#include <cstdio>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render::tests {
namespace {

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

[[nodiscard]] RenderMaterialGraphShaderSource CompileConstantColorGraph(
    std::string_view colorHint,
    std::string_view shadingModel = "defaultLit",
    std::string_view blendMode = "opaque") {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = std::string{ shadingModel };
    graph.blendMode = std::string{ blendMode };
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 80,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = std::string{ colorHint } },
    });
    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::ConstantColor, 2U, "rgba", RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0400U });
    Require(compiled.Succeeded(), "KBMAT-MAT04: Constant color graph must compile before cooking");
    return compiled.shader;
}

[[nodiscard]] RenderMaterialGraphShaderSource CompileMaskedWorldPositionOffsetGraph() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.blendMode = "masked";
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = -220,
        .positionY = 40,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.25 0 0" },
    });
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantScalar,
        .positionX = -220,
        .positionY = 180,
        .parameter = RenderMaterialGraphParameterMetadata{ .defaultValueHint = "0.25" },
    });
    graph.links.push_back(MakeGraphLink(
        RenderMaterialGraphNodeKind::ConstantVector, 2U, "xyz",
        RenderMaterialGraphNodeKind::MaterialOutput, 1U, "worldPositionOffset"));
    graph.links.push_back(MakeGraphLink(
        RenderMaterialGraphNodeKind::ConstantScalar, 3U, "value",
        RenderMaterialGraphNodeKind::MaterialOutput, 1U, "alpha"));
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
        graph,
        RenderMaterialGraphBuildContext{ .assetId = 0x5005U });
    Require(compiled.Succeeded() && compiled.shader.reflection.hasWorldPositionOffset &&
            compiled.shader.reflection.blendMode == RenderMaterialGraphBlendMode::Masked,
        "P0.5: masked WPO graph must compile with both shadow requirements");
    return compiled.shader;
}

void RunGraphShaderWrapperSourceTest() {
    const RenderMaterialGraphShaderSource shader = CompileConstantColorGraph("0.25 0.5 0.75 1");

    const std::string opaque = BuildGraphFragmentWrapperSource(shader, "BaseOpaque");
    Require(opaque.find("$input v_normal, v_color0, v_texcoord0, v_worldPos, v_shadowPos, v_shadowFlags, v_tangent, v_bitangent") != std::string::npos,
        "KBMAT-MAT04: Graph fragment wrapper must declare bgfx varyings matching the mesh vertex shader outputs");
    Require(opaque.find("#include <bgfx_shader.sh>") != std::string::npos,
        "KBMAT-MAT04: Graph fragment wrapper must include the bgfx shader header");
    Require(opaque.find("MaterialSurface surface = EvaluateMaterialGraph(ctx);") != std::string::npos,
        "KBMAT-MAT04: Graph fragment wrapper must call the generated surface function");
    Require(opaque.find("void main()") != std::string::npos,
        "KBMAT-MAT04: Graph fragment wrapper must provide a fragment entry point");
    Require(opaque.find("#include \"pbr_graph_forward.sh\"") != std::string::npos,
        "KBMAT-MAT08: Forward wrapper must reuse the shared PBR lighting library");
    Require(opaque.find("KbEvaluateForwardLighting(") != std::string::npos,
        "KBMAT-MAT08: Forward wrapper must run PBR lighting over the evaluated surface");
    Require(opaque.find("uniform vec4 u_materialParams;") != std::string::npos &&
            opaque.find("graphNormal.xy * u_materialParams.z") != std::string::npos &&
            opaque.find("basisTangent * graphNormal.x") != std::string::npos,
        "KBMAT-MAT87: Forward graph normal mapping must honor material normalScale before TBN lighting");
    Require(opaque.find("ctx.twoSidedSign = gl_FrontFacing ? 1.0 : -1.0;") != std::string::npos,
        "KBMAT-MAT46: Forward wrapper must expose a real front/back-face sign to graph nodes");
    // The scene colour alpha is the share of the opaque surface still visible (the TAA reactive mask): an opaque
    // surface covers what is behind it, only a translucent one writes its opacity.
    Require(opaque.find("gl_FragColor = vec4(lighting + surface.emissive, terrainLayerOpacity);") != std::string::npos,
        "KBMAT-MAT08: Forward wrapper must combine lit color and emissive, covering what is behind an opaque surface");
    const std::string translucent = BuildGraphFragmentWrapperSource(
        CompileConstantColorGraph("0.25 0.5 0.75 0.5", "defaultLit", "translucent"), "BaseTransparent");
    Require(translucent.find("gl_FragColor = vec4(lighting + surface.emissive, surface.alpha);") != std::string::npos,
        "KBMAT-MAT08: A translucent forward wrapper must write its surface opacity");
    Require(opaque.find("basisTangent = vertexTangent;") != std::string::npos &&
            opaque.find("basisBitangent = vertexBitangent;") != std::string::npos &&
            opaque.find("basisTangent = derivativeTangent") == std::string::npos &&
            opaque.find("dFdx(v_texcoord0)") == std::string::npos,
        "KBMAT-MAT87: Forward graph normal mapping must use the mesh vertex TBN, not a screen-derivative UV basis");

    const std::string shadow = BuildGraphFragmentWrapperSource(shader, "ShadowDepth");
    Require(shadow.find("gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);") != std::string::npos,
        "KBMAT-MAT04: ShadowDepth wrapper must not depend on full surface shading output");
    Require(shadow.find("surface.alphaClipThreshold") != std::string::npos,
        "KBMAT-MAT04: ShadowDepth wrapper must still honor the graph alpha clip contract");
    const std::string gbuffer = BuildGraphFragmentWrapperSource(shader, "GBuffer");
    Require(gbuffer.find("// pass:GBuffer") != std::string::npos,
        "KBMAT-MAT04: GBuffer wrapper must carry its own pass identity");
    Require(gbuffer.find("#include \"gbuffer_contract.sh\"") != std::string::npos &&
            gbuffer.find("gl_FragData[0] = vec4(surface.baseColor.rgb, clamp(surface.clearCoat, 0.0, 1.0));") != std::string::npos &&
            gbuffer.find("gl_FragData[1] = vec4(worldNormal * 0.5 + 0.5, KbEncodeGBufferCustomData1(surface.clearCoatRoughness));") != std::string::npos &&
            gbuffer.find("gl_FragData[2] = vec4(clamp(surface.metallic") != std::string::npos &&
            gbuffer.find("KbEncodeGBufferShadingModel(1.0)") != std::string::npos &&
            gbuffer.find("gl_FragData[3] = vec4(surface.emissive, clamp(surface.specular, 0.0, 1.0));") != std::string::npos,
        "Deferred graph wrapper must write albedo, normal, material/shading-model and emissive/specular MRT outputs");
    Require(gbuffer.find("graphNormal.xy * u_materialParams.z") != std::string::npos &&
            gbuffer.find("basisTangent * graphNormal.x") != std::string::npos,
        "KBMAT-MAT87: GBuffer graph normal mapping must honor material normalScale before writing the normal MRT");
    Require(gbuffer.find("KbEvaluateForwardLighting(") == std::string::npos,
        "Deferred graph GBuffer wrapper must not light through the forward shader");
    Require(gbuffer.find("basisTangent = vertexTangent;") != std::string::npos &&
            gbuffer.find("basisBitangent = vertexBitangent;") != std::string::npos &&
            gbuffer.find("basisTangent = derivativeTangent") == std::string::npos &&
            gbuffer.find("dFdx(v_texcoord0)") == std::string::npos,
        "KBMAT-MAT87: GBuffer graph normal mapping must use the mesh vertex TBN, not a screen-derivative UV basis");
    Require(opaque != shadow && opaque != gbuffer && shadow != gbuffer, "KBMAT-MAT04: Different passes must produce different wrapper sources");

    const RenderMaterialGraphShaderSource unlitShader = CompileConstantColorGraph("0.25 0.5 0.75 1", "unlit");
    const std::string unlitGBuffer = BuildGraphFragmentWrapperSource(unlitShader, "GBuffer");
    Require(unlitGBuffer.find("KbEncodeGBufferShadingModel(0.0)") != std::string::npos &&
            unlitGBuffer.find("gl_FragData[3] = vec4(surface.emissive, clamp(surface.specular, 0.0, 1.0));") != std::string::npos,
        "P0.6: Unlit GBuffer writer must preserve the stable shading-model id, emissive and explicit specular");
    Require(unlitGBuffer.find("KbEvaluateForwardLighting(") == std::string::npos,
        "P0.6: Unlit GBuffer geometry pass must defer shading without running forward PBR");
}

void RunGraphShaderBackendMetadataTest() {
    Require(RenderMaterialGraphShaderBackendProfile(RenderMaterialGraphShaderBackend::Spirv) == std::string_view{ "spirv" },
        "KBMAT-MAT04: SPIR-V backend must map to the spirv shaderc profile");
    Require(kb::assets::bake::ShaderBakePlatformName(kb::assets::bake::ShaderBakePlatform::Windows) ==
            std::string_view{ "windows" },
        "KBMAT-MAT04: Windows shader platform must use shaderc's canonical spelling");
    Require(RenderMaterialGraphShaderBackendDirectory(RenderMaterialGraphShaderBackend::Glsl) == std::string_view{ "glsl" },
        "KBMAT-MAT04: Backend staging directory must match the backend name");
    Require(ParseRenderMaterialGraphShaderBackend("dxil") == RenderMaterialGraphShaderBackend::Dxil,
        "KBMAT-MAT04: Backend parsing must round-trip");
    Require(!ParseRenderMaterialGraphShaderBackend("nope").has_value(),
        "KBMAT-MAT04: Unknown backend name must not parse");
}

void RunGraphShaderReflectionHashTest() {
    const RenderMaterialGraphShaderSource shaderA = CompileConstantColorGraph("0.25 0.5 0.75 1");
    const std::uint64_t hashA = ComputeRenderMaterialGraphReflectionHash(shaderA.reflection);
    const std::uint64_t hashAgain = ComputeRenderMaterialGraphReflectionHash(shaderA.reflection);
    Require(hashA == hashAgain, "KBMAT-MAT04: Reflection hash must be stable for the same reflection");

    RenderMaterialGraphReflection extended = shaderA.reflection;
    extended.textures.push_back(RenderMaterialGraphReflectionTexture{ .samplerName = "u_extra_texture", .stableId = "extra", .slot = 0U });
    Require(ComputeRenderMaterialGraphReflectionHash(extended) != hashA,
        "KBMAT-MAT04: Reflection hash must change when bindings change");

    RenderMaterialGraphReflection boundaryA{};
    boundaryA.uniforms.push_back(RenderMaterialGraphReflectionUniform{ .name = "ab", .stableId = "c" });
    RenderMaterialGraphReflection boundaryB{};
    boundaryB.uniforms.push_back(RenderMaterialGraphReflectionUniform{ .name = "a", .stableId = "bc" });
    Require(ComputeRenderMaterialGraphReflectionHash(boundaryA) != ComputeRenderMaterialGraphReflectionHash(boundaryB),
        "P0.7: reflection identity must preserve field boundaries instead of hashing ambiguous string concatenation");

    RenderMaterialGraphShaderSource unlit = shaderA;
    unlit.reflection.shadingModel = RenderMaterialShadingModel::Unlit;
    Require(ComputeRenderMaterialGraphVariantKey(unlit) != ComputeRenderMaterialGraphVariantKey(shaderA),
        "P0.7: canonical variant identity must include wrapper-affecting shading model reflection");
    RenderMaterialGraphShaderSource masked = shaderA;
    masked.reflection.blendMode = RenderMaterialGraphBlendMode::Masked;
    Require(ComputeRenderMaterialGraphVariantKey(masked) != ComputeRenderMaterialGraphVariantKey(shaderA),
        "P0.7: canonical variant identity must include wrapper-affecting blend mode reflection");
}

void RunGraphShaderManifestValidationTest() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "kb_graph_shader_manifest_test";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const std::filesystem::path existing = root / "fs_present.bin";
    {
        std::ofstream out{ existing, std::ios::binary | std::ios::trunc };
        out << "BGFXBINARY";
    }

    RenderMaterialGraphShaderArtifact good{};
    good.graphSourceHash = 0x1234U;
    good.pass = "BaseOpaque";
    good.graphGenerated = true;
    good.binaries.push_back(RenderMaterialGraphShaderBinary{ .backend = RenderMaterialGraphShaderBackend::Spirv, .binaryPath = existing.generic_string(), .byteSize = 10U });

    RenderMaterialGraphShaderArtifact missing{};
    missing.graphSourceHash = 0x5678U;
    missing.pass = "BaseOpaque";
    missing.graphGenerated = true;
    missing.binaries.push_back(RenderMaterialGraphShaderBinary{ .backend = RenderMaterialGraphShaderBackend::Dxbc, .binaryPath = (root / "does_not_exist.bin").generic_string(), .byteSize = 0U });

    RenderMaterialGraphShaderArtifact builtin{};
    builtin.graphSourceHash = 0U;
    builtin.pass = "BaseOpaque";
    builtin.graphGenerated = false;
    builtin.binaries.push_back(RenderMaterialGraphShaderBinary{ .backend = RenderMaterialGraphShaderBackend::Spirv, .binaryPath = "fs_mesh_instanced", .byteSize = 0U });

    const std::array<RenderMaterialGraphShaderArtifact, 3U> artifacts{ good, missing, builtin };
    const RenderMaterialGraphShaderManifest manifest = BuildRenderMaterialGraphShaderManifest(artifacts);
    Require(manifest.entries.size() == 3U, "KBMAT-MAT04: Manifest must contain one entry per binary");
    Require(manifest.manifestHash != 0U, "KBMAT-MAT04: Manifest must expose a content hash");
    Require(manifest.Find(0x1234U, "BaseOpaque", RenderMaterialGraphShaderBackend::Spirv) != nullptr,
        "KBMAT-MAT04: Manifest must be queryable by graph hash/pass/backend");

    const std::vector<RenderMaterialGraphDiagnostic> diagnostics = ValidateRenderMaterialGraphShaderManifest(manifest);
    Require(diagnostics.size() == 1U,
        "KBMAT-MAT04: Manifest validation must flag exactly the missing graph binary, ignoring builtin static shaders");
    Require(diagnostics[0].message.find("does_not_exist.bin") != std::string::npos,
        "KBMAT-MAT04: Manifest validation diagnostic must identify the missing binary");

    std::filesystem::remove_all(root, error);
}

#if defined(KB_TEST_GRAPH_SHADERC_PATH)
[[nodiscard]] RenderMaterialGraphShaderArtifactRequest MakeCookRequest(std::string pass) {
    RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
    request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
    request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
    request.cacheRoot = KB_TEST_GRAPH_SHADER_CACHE_DIR;
    request.pass = std::move(pass);
    request.shaderPlatform = kb::assets::bake::ShaderBakePlatform::Linux;
    return request;
}

[[nodiscard]] std::filesystem::path TestGraphCacheRoot(std::uint64_t sourceHash) {
    return std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "linux" /
        ("graph_" + std::to_string(sourceHash));
}

void RunGraphShaderCookProducesBinaryTest() {
    const RenderMaterialGraphShaderSource shader = CompileConstantColorGraph("0.2 0.4 0.6 1");
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };

    std::error_code error;
    std::filesystem::remove_all(TestGraphCacheRoot(shader.sourceHash), error);

    const RenderMaterialGraphShaderArtifactResult first = CookRenderMaterialGraphShaderArtifact(shader, backends, MakeCookRequest("BaseOpaque"));
    Require(first.Succeeded(), "KBMAT-MAT04: Cooking a simple graph material must succeed");
    Require(first.artifact.has_value(), "KBMAT-MAT04: Cook must produce an artifact");
    const RenderMaterialGraphShaderBinary* spirv = first.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv);
    Require(spirv != nullptr, "KBMAT-MAT04: Cook must produce a SPIR-V graph fragment binary");
    Require(!spirv->cacheHit, "KBMAT-MAT04: First cook of a fresh graph must be a real shaderc compile, not a cache hit");
    Require(std::filesystem::exists(spirv->binaryPath, error) && std::filesystem::file_size(spirv->binaryPath, error) > 0U,
        "KBMAT-MAT04: Cooked graph fragment binary must exist on disk in the staging cache");
    const std::filesystem::path graphCacheRoot = TestGraphCacheRoot(shader.sourceHash);
    const bool leftCompilerLog = std::ranges::any_of(
        std::filesystem::recursive_directory_iterator(graphCacheRoot),
        [](const std::filesystem::directory_entry& entry) {
            const std::string name = entry.path().filename().string();
            return entry.is_regular_file() &&
                (name.ends_with(".shaderc.log") || name.ends_with(".shaderc.tmp"));
        });
    Require(!leftCompilerLog,
        "Material graph cook must not leave shaderc debug or temporary logs in the artifact cache");

    const std::array<RenderMaterialGraphShaderArtifact, 1U> manifestArtifacts{ *first.artifact };
    const RenderMaterialGraphShaderManifest manifest = BuildRenderMaterialGraphShaderManifest(manifestArtifacts);
    Require(ValidateRenderMaterialGraphShaderManifest(manifest).empty(),
        "KBMAT-MAT04: A freshly cooked manifest must validate against on-disk binaries");

    const RenderMaterialGraphShaderArtifactResult second = CookRenderMaterialGraphShaderArtifact(shader, backends, MakeCookRequest("BaseOpaque"));
    Require(second.Succeeded() && second.artifact.has_value(), "KBMAT-MAT04: Rebuilding an unchanged graph must succeed");
    const RenderMaterialGraphShaderBinary* cachedSpirv = second.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv);
    Require(cachedSpirv != nullptr && cachedSpirv->cacheHit,
        "KBMAT-MAT04: Rebuilding an unchanged graph must hit the shader binary cache");

    {
        std::ofstream corrupt{ cachedSpirv->binaryPath, std::ios::binary | std::ios::trunc };
        corrupt << "corrupt";
    }
    const RenderMaterialGraphShaderArtifactResult repaired =
        CookRenderMaterialGraphShaderArtifact(shader, backends, MakeCookRequest("BaseOpaque"));
    const RenderMaterialGraphShaderBinary* repairedSpirv = repaired.artifact.has_value()
        ? repaired.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)
        : nullptr;
    Require(repaired.Succeeded() && repairedSpirv != nullptr && !repairedSpirv->cacheHit &&
            repairedSpirv->byteSize > 7U,
        "Corrupt material graph cache binary must be rejected and rebuilt from shader inputs");

    const RenderMaterialGraphShaderArtifactResult gbuffer = CookRenderMaterialGraphShaderArtifact(shader, backends, MakeCookRequest("GBuffer"));
    Require(gbuffer.Succeeded() && gbuffer.artifact.has_value(), "Deferred graph GBuffer cook must produce a shader binary");
    const RenderMaterialGraphShaderBinary* gbufferSpirv = gbuffer.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv);
    Require(gbufferSpirv != nullptr &&
            (gbufferSpirv->binaryPath.find("/GBuffer/") != std::string::npos ||
             gbufferSpirv->binaryPath.find("\\GBuffer\\") != std::string::npos),
        "Deferred graph GBuffer cook must use a distinct GBuffer artifact directory");
    const std::array<RenderMaterialGraphShaderArtifact, 1U> gbufferManifestArtifacts{ *gbuffer.artifact };
    const RenderMaterialGraphShaderManifest gbufferManifest = BuildRenderMaterialGraphShaderManifest(gbufferManifestArtifacts);
    Require(gbufferManifest.Find(shader.sourceHash, "GBuffer", RenderMaterialGraphShaderBackend::Spirv) != nullptr,
        "Deferred graph GBuffer artifact must be discoverable in the shader manifest");
}

void RunGraphShaderVariantArtifactCoexistenceTest() {
    const RenderMaterialGraphShaderSource defaultOpaque = CompileConstantColorGraph("0.35 0.45 0.55 1");
    RenderMaterialGraphShaderSource masked = defaultOpaque;
    masked.reflection.blendMode = RenderMaterialGraphBlendMode::Masked;
    RenderMaterialGraphShaderSource unlit = defaultOpaque;
    unlit.reflection.shadingModel = RenderMaterialShadingModel::Unlit;

    Require(defaultOpaque.sourceHash == masked.sourceHash && defaultOpaque.sourceHash == unlit.sourceHash,
        "P0.7: variant regression requires identical graph source hashes");
    const std::array<std::uint64_t, 3U> identities{
        ComputeRenderMaterialGraphVariantKey(defaultOpaque),
        ComputeRenderMaterialGraphVariantKey(masked),
        ComputeRenderMaterialGraphVariantKey(unlit),
    };
    Require(identities[0] != identities[1] && identities[0] != identities[2] && identities[1] != identities[2],
        "P0.7: opaque/masked and DefaultLit/Unlit variants must have distinct canonical identities");

    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };
    const std::array<const RenderMaterialGraphShaderSource*, 3U> forwardOrder{ &defaultOpaque, &masked, &unlit };
    const std::array<const RenderMaterialGraphShaderSource*, 3U> reverseOrder{ &unlit, &masked, &defaultOpaque };
    const auto cookOrder = [&](std::string_view suffix, const auto& order) {
        const std::filesystem::path root = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } /
            ("p07_variants_" + std::string{ suffix });
        std::error_code error;
        std::filesystem::remove_all(root, error);
        std::array<std::filesystem::path, 3U> paths;
        for (const RenderMaterialGraphShaderSource* shader : order) {
            RenderMaterialGraphShaderArtifactRequest request = MakeCookRequest("BaseOpaque");
            request.cacheRoot = root.generic_string();
            const RenderMaterialGraphShaderArtifactResult cooked =
                CookRenderMaterialGraphShaderArtifact(*shader, backends, request);
            Require(cooked.Succeeded() && cooked.artifact.has_value(),
                "P0.7: variant cook failed");
            const RenderMaterialGraphShaderBinary* binary =
                cooked.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv);
            Require(binary != nullptr && cooked.artifact->variantKey == ComputeRenderMaterialGraphVariantKey(*shader),
                "P0.7: cooked artifact lost canonical variant identity");
            const auto identity = std::ranges::find(identities, cooked.artifact->variantKey);
            Require(identity != identities.end(), "P0.7: cooked unexpected variant identity");
            const std::size_t index = static_cast<std::size_t>(std::distance(identities.begin(), identity));
            paths[index] = binary->binaryPath;
        }
        Require(paths[0] != paths[1] && paths[0] != paths[2] && paths[1] != paths[2],
            "P0.7: variants must never share a physical binary path");
        for (std::size_t index = 0U; index < paths.size(); ++index) {
            Require(std::filesystem::exists(paths[index], error) && std::filesystem::file_size(paths[index], error) > 0U &&
                    paths[index].generic_string().find("variant_" + std::to_string(identities[index])) != std::string::npos,
                "P0.7: cooking another variant overwrote or hid an existing binary");
        }
        RenderMaterialGraphShaderArtifactRequest repeatRequest = MakeCookRequest("BaseOpaque");
        repeatRequest.cacheRoot = root.generic_string();
        const RenderMaterialGraphShaderArtifactResult repeated =
            CookRenderMaterialGraphShaderArtifact(defaultOpaque, backends, repeatRequest);
        const RenderMaterialGraphShaderBinary* repeatedBinary = repeated.artifact.has_value()
            ? repeated.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)
            : nullptr;
        Require(repeated.Succeeded() && repeatedBinary != nullptr && repeatedBinary->cacheHit &&
                repeatedBinary->binaryPath == paths[0].generic_string(),
            "P0.7: an A-B-A variant cook must reuse A without publishing B under A's key");
    };

    cookOrder("forward", forwardOrder);
    cookOrder("reverse", reverseOrder);
}

void RunGraphShaderSameKeyConcurrentCookTest() {
    const RenderMaterialGraphShaderSource shader = CompileConstantColorGraph("0.12 0.34 0.56 1");
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{
        RenderMaterialGraphShaderBackend::Spirv };
    const std::filesystem::path cacheRoot =
        std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "p1_same_key_concurrency";
    std::error_code error;
    std::filesystem::remove_all(cacheRoot, error);

    std::array<std::future<RenderMaterialGraphShaderArtifactResult>, 4U> cooks;
    for (auto& cook : cooks) {
        cook = std::async(std::launch::async, [shader, backends, cacheRoot]() {
            RenderMaterialGraphShaderArtifactRequest request = MakeCookRequest("BaseOpaque");
            request.cacheRoot = cacheRoot.generic_string();
            return CookRenderMaterialGraphShaderArtifact(shader, backends, request);
        });
    }
    std::size_t cacheMisses = 0U;
    std::filesystem::path publishedPath;
    for (auto& cook : cooks) {
        const RenderMaterialGraphShaderArtifactResult result = cook.get();
        const RenderMaterialGraphShaderBinary* binary = result.artifact.has_value()
            ? result.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)
            : nullptr;
        Require(result.Succeeded() && binary != nullptr &&
                std::filesystem::is_regular_file(binary->binaryPath, error) &&
                std::filesystem::file_size(binary->binaryPath, error) > 0U,
            "Concurrent same-key graph cooks must all observe a complete published binary");
        cacheMisses += binary->cacheHit ? 0U : 1U;
        if (publishedPath.empty()) {
            publishedPath = binary->binaryPath;
        } else {
            Require(publishedPath == binary->binaryPath,
                "Concurrent same-key graph cooks must agree on one keyed cache path");
        }
    }
    Require(cacheMisses == 1U,
        "Concurrent same-key graph cooks must compile once and serialize all cache readers");
}

void RunGraphShadowDepthVertexAndAlphaCookTest() {
    const RenderMaterialGraphShaderSource shader = CompileMaskedWorldPositionOffsetGraph();
    const std::filesystem::path root = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "p05_shadow_depth";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    RenderMaterialGraphShaderArtifactRequest request = MakeCookRequest("ShadowDepth");
    request.cacheRoot = root.generic_string();
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };
    const RenderMaterialGraphShaderArtifactResult cooked = CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    Require(cooked.Succeeded() && cooked.artifact.has_value() && cooked.artifact->hasVertexShader,
        "P0.5: ShadowDepth cook must produce a generated vertex shader for WPO/displacement");
    const RenderMaterialGraphShaderBinary* fragment = cooked.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv);
    const RenderMaterialGraphShaderBinary* vertex = cooked.artifact->FindVertexBinary(RenderMaterialGraphShaderBackend::Spirv);
    Require(fragment != nullptr && vertex != nullptr &&
            std::filesystem::exists(fragment->binaryPath, error) && std::filesystem::exists(vertex->binaryPath, error),
        "P0.5: ShadowDepth artifact must contain loadable fragment and generated vertex binaries");
    Require(cooked.artifact->wrapperSource.find("surface.alpha < surface.alphaClipThreshold") != std::string::npos &&
            cooked.artifact->vertexWrapperSource.find("EvaluateWorldPositionOffset(ctx)") != std::string::npos,
        "P0.5: ShadowDepth artifact must preserve masked graph alpha clip and WPO evaluation");
}

void RunGraphShaderCookShadercFailureTest() {
    RenderMaterialGraphShaderSource broken{};
    broken.entryPoint = "EvaluateMaterialGraph";
    broken.sourceHash = 0xDEADBEEFU;
    broken.source = "this is definitely not valid shader code !!!\n";

    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };
    std::error_code error;
    std::filesystem::remove_all(TestGraphCacheRoot(broken.sourceHash), error);

    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(broken, backends, MakeCookRequest("BaseOpaque"));
    Require(!result.Succeeded(), "KBMAT-MAT04: A graph that fails shaderc compilation must not report success");
    bool hasError = false;
    for (const RenderMaterialGraphDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error) {
            hasError = true;
        }
    }
    Require(hasError, "KBMAT-MAT04: shaderc compilation failure must produce an error diagnostic");
    const std::filesystem::path brokenCacheRoot = TestGraphCacheRoot(broken.sourceHash);
    const bool leftFailureLog = std::filesystem::exists(brokenCacheRoot, error) && std::ranges::any_of(
        std::filesystem::recursive_directory_iterator(brokenCacheRoot),
        [](const std::filesystem::directory_entry& entry) {
            const std::string name = entry.path().filename().string();
            return entry.is_regular_file() &&
                (name.ends_with(".shaderc.log") || name.ends_with(".shaderc.tmp"));
        });
    Require(!leftFailureLog,
        "Failed material graph cook must return diagnostics without leaving shaderc log files");
}

void RunGraphShaderArtifactDependencyInvalidationTest() {
    const RenderMaterialGraphShaderSource shader = CompileConstantColorGraph("0.3 0.6 0.9 1");
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };

    const std::filesystem::path cacheRoot = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat13_dependency";
    std::error_code error;
    std::filesystem::remove_all(cacheRoot, error);
    std::filesystem::create_directories(cacheRoot, error);
    const std::filesystem::path dependency = cacheRoot / "graph_dependency.sh";
    {
        std::ofstream out{ dependency, std::ios::binary | std::ios::trunc };
        out << "// graph dependency v1\n";
    }

    RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
    request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
    request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
    request.dependencyFiles = { dependency.generic_string() };
    request.cacheRoot = cacheRoot.generic_string();
    request.pass = "BaseOpaque";
    request.shaderPlatform = kb::assets::bake::ShaderBakePlatform::Linux;
    request.materialTypeVersion = 1U;

    const RenderMaterialGraphShaderArtifactResult first = CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    Require(first.Succeeded() && first.artifact.has_value(), "KBMAT-MAT13: Cook with a dependency must succeed");
    Require(first.artifact->dependencyHash != 0U && first.artifact->artifactHash != 0U,
        "KBMAT-MAT13: Artifact must expose a dependency hash and a combined artifact hash");
    Require(first.artifact->dependencies.size() >= 1U,
        "KBMAT-MAT13: Artifact dependency graph must include the shader wrapper includes");
    const std::uint64_t artifactHashBefore = first.artifact->artifactHash;

    const RenderMaterialGraphShaderArtifactResult rebuild = CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    Require(rebuild.artifact.has_value() && rebuild.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv) != nullptr &&
            rebuild.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)->cacheHit,
        "KBMAT-MAT13: Rebuilding with unchanged dependencies must hit the artifact cache");

    // Editing a wrapper dependency must invalidate the artifact and force a recompile.
    {
        std::ofstream out{ dependency, std::ios::binary | std::ios::trunc };
        out << "// graph dependency v2 changed\n";
    }
    const RenderMaterialGraphShaderArtifactResult afterChange = CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    Require(afterChange.Succeeded() && afterChange.artifact.has_value(), "KBMAT-MAT13: Cook after dependency change must succeed");
    Require(afterChange.artifact->artifactHash != artifactHashBefore,
        "KBMAT-MAT13: Changing a wrapper dependency must change the artifact hash");
    const RenderMaterialGraphShaderBinary* changedBinary = afterChange.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv);
    Require(changedBinary != nullptr && !changedBinary->cacheHit,
        "KBMAT-MAT13: Changing a wrapper dependency must invalidate the cooked binary and force a recompile");
}

void RunGraphShaderAutomaticIncludeInvalidationTest() {
    const RenderMaterialGraphShaderSource shader = CompileConstantColorGraph("0.7 0.2 0.4 1");
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };
    const std::filesystem::path cacheRoot =
        std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "p07_automatic_include_dependency";
    const std::filesystem::path includeRoot = cacheRoot / "includes";
    const std::filesystem::path sourceInclude =
        std::filesystem::path{ KB_TEST_GRAPH_SHADER_INCLUDE_DIR } / "pbr_graph_forward.sh";
    const std::filesystem::path copiedInclude = includeRoot / "pbr_graph_forward.sh";
    std::error_code error;
    std::filesystem::remove_all(cacheRoot, error);
    error.clear();
    std::filesystem::create_directories(includeRoot, error);
    Require(!error, "P0.7: automatic include dependency test could not create its cache root");
    Require(std::filesystem::copy_file(sourceInclude, copiedInclude, std::filesystem::copy_options::overwrite_existing, error) && !error,
        "P0.7: automatic include dependency test could not copy the shared PBR include");

    RenderMaterialGraphShaderArtifactRequest request = MakeCookRequest("BaseOpaque");
    request.cacheRoot = cacheRoot.generic_string();
    request.includeDirs.insert(request.includeDirs.begin(), includeRoot.generic_string());

    const RenderMaterialGraphShaderArtifactResult first = CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    Require(first.Succeeded() && first.artifact.has_value(),
        "P0.7: graph cook with an automatically discovered shared include must succeed");
    const auto hasDependency = [&](std::string_view name) {
        return std::ranges::any_of(first.artifact->dependencies, [name](const RenderMaterialGraphArtifactDependency& dependency) {
            return dependency.name == name;
        });
    };
    Require(hasDependency("pbr_graph_forward.sh") && hasDependency("bgfx_shader.sh"),
        "P0.7: artifact identity must automatically include direct and transitive shader includes");

    const RenderMaterialGraphShaderArtifactResult unchanged = CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    const RenderMaterialGraphShaderBinary* unchangedBinary = unchanged.artifact.has_value()
        ? unchanged.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)
        : nullptr;
    Require(unchanged.Succeeded() && unchangedBinary != nullptr && unchangedBinary->cacheHit,
        "P0.7: unchanged automatic include dependencies must preserve a cache hit");

    {
        std::ofstream output{ copiedInclude, std::ios::binary | std::ios::app };
        output << "\n// P0.7 dependency invalidation regression\n";
    }
    const RenderMaterialGraphShaderArtifactResult changed = CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    const RenderMaterialGraphShaderBinary* changedBinary = changed.artifact.has_value()
        ? changed.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)
        : nullptr;
    Require(changed.Succeeded() && changed.artifact.has_value() && changedBinary != nullptr && !changedBinary->cacheHit &&
            changed.artifact->dependencyHash != first.artifact->dependencyHash &&
            changed.artifact->artifactHash != first.artifact->artifactHash,
        "P0.7: editing a shared include must invalidate the binary and complete artifact identity without manual dependency registration");
}

void RunGraphShaderToolchainIdentityInvalidationTest() {
    const RenderMaterialGraphShaderSource shader = CompileConstantColorGraph("0.15 0.35 0.75 1");
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };
    const std::filesystem::path cacheRoot =
        std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "p13_toolchain_identity";
    std::error_code error;
    std::filesystem::remove_all(cacheRoot, error);
    std::filesystem::create_directories(cacheRoot, error);

    const std::filesystem::path shadercCopy = cacheRoot /
        (std::filesystem::path{ KB_TEST_GRAPH_SHADERC_PATH }.extension().empty() ? "shaderc-copy" : "shaderc-copy.exe");
    Require(std::filesystem::copy_file(
                KB_TEST_GRAPH_SHADERC_PATH,
                shadercCopy,
                std::filesystem::copy_options::overwrite_existing,
                error) && !error,
        "P1.3: toolchain identity test could not copy shaderc");

    RenderMaterialGraphShaderArtifactRequest request = MakeCookRequest("BaseOpaque");
    request.cacheRoot = cacheRoot.generic_string();
    request.shadercPath = shadercCopy.generic_string();
    const RenderMaterialGraphShaderArtifactResult first =
        CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    Require(first.Succeeded() && first.artifact.has_value(),
        "P1.3: initial cook with an explicit shaderc identity must succeed");

    const RenderMaterialGraphShaderArtifactResult unchanged =
        CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    const RenderMaterialGraphShaderBinary* unchangedBinary = unchanged.artifact.has_value()
        ? unchanged.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)
        : nullptr;
    Require(unchangedBinary != nullptr && unchangedBinary->cacheHit,
        "P1.3: an unchanged shaderc and include search path must preserve the cache hit");

    std::ranges::reverse(request.includeDirs);
    const RenderMaterialGraphShaderArtifactResult reordered =
        CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    const RenderMaterialGraphShaderBinary* reorderedBinary = reordered.artifact.has_value()
        ? reordered.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)
        : nullptr;
    Require(reordered.Succeeded() && reorderedBinary != nullptr && !reorderedBinary->cacheHit &&
            reordered.artifact->dependencyHash != first.artifact->dependencyHash,
        "P1.3: changing ordered include-directory resolution must force a recompile");

    {
        std::ofstream mutateTool{ shadercCopy, std::ios::binary | std::ios::app };
        mutateTool.put('\0');
    }
    const RenderMaterialGraphShaderArtifactResult changedTool =
        CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    const RenderMaterialGraphShaderBinary* changedToolBinary = changedTool.artifact.has_value()
        ? changedTool.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)
        : nullptr;
    Require(changedTool.Succeeded() && changedToolBinary != nullptr && !changedToolBinary->cacheHit &&
            changedTool.artifact->dependencyHash != reordered.artifact->dependencyHash,
        "P1.3: changing the shaderc binary identity must force a recompile");
}
#endif

void RunGraphShaderManifestRoundTripTest() {
    RenderMaterialGraphShaderArtifact artifactA{};
    artifactA.graphSourceHash = 0x1111U;
    artifactA.variantKey = 0x1212U;
    artifactA.wrapperHash = 0x2222U;
    artifactA.reflectionHash = 0x3333U;
    artifactA.dependencyHash = 0x4444U;
    artifactA.artifactHash = 0x5555U;
    artifactA.materialTypeVersion = 2U;
    artifactA.pass = "BaseOpaque";
    artifactA.graphGenerated = true;
    artifactA.binaries.push_back(RenderMaterialGraphShaderBinary{ .backend = RenderMaterialGraphShaderBackend::Dxbc, .binaryPath = "cache/graph_1/BaseOpaque/dxbc/fs.bin" });
    artifactA.binaries.push_back(RenderMaterialGraphShaderBinary{ .backend = RenderMaterialGraphShaderBackend::Spirv, .binaryPath = "cache/graph_1/BaseOpaque/spirv/fs.bin" });

    const std::array<RenderMaterialGraphShaderArtifact, 1U> artifacts{ artifactA };
    const RenderMaterialGraphShaderManifest manifest = BuildRenderMaterialGraphShaderManifest(artifacts);

    std::ostringstream output;
    WriteRenderMaterialGraphShaderManifest(output, manifest);
    Require(output.str().find("graphArtifact ") != std::string::npos, "KBMAT-MAT13: Manifest writer must emit graph artifact rows");

    std::istringstream input{ output.str() };
    const RenderMaterialGraphShaderManifest parsed = ParseRenderMaterialGraphShaderManifest(input);
    Require(parsed.manifestHash == manifest.manifestHash, "KBMAT-MAT13: Manifest round-trip must preserve the manifest hash");
    Require(parsed.entries.size() == manifest.entries.size(), "KBMAT-MAT13: Manifest round-trip must preserve every entry");

    const RenderMaterialGraphShaderManifestEntry* dxbc = parsed.Find(0x1111U, 0x1212U, "BaseOpaque", RenderMaterialGraphShaderBackend::Dxbc);
    Require(dxbc != nullptr, "KBMAT-MAT13: Round-tripped manifest must remain queryable by graph hash/pass/backend");
    Require(dxbc->variantKey == 0x1212U && dxbc->wrapperHash == 0x2222U && dxbc->reflectionHash == 0x3333U && dxbc->dependencyHash == 0x4444U &&
            dxbc->artifactHash == 0x5555U && dxbc->materialTypeVersion == 2U,
        "KBMAT-MAT13: Manifest round-trip must preserve the artifact identity hashes");
    Require(dxbc->binaryPath == "cache/graph_1/BaseOpaque/dxbc/fs.bin",
        "KBMAT-MAT13: Manifest round-trip must preserve the per-backend binary path");
}

} // namespace

// A project the user keeps deep in their own folders pushes the shader cache past Win32's
// MAX_PATH of 260 characters. std::filesystem creates those directories through the
// extended-length form on its own, so the cook used to get all the way to the raw Win32 calls
// under them and refuse there: first the publication lock, then everything the external compiler
// had to open. The cache is now addressed in extended-length form and shaderc works in a scratch
// directory of its own, so a deep cache root cooks and re-hits exactly like a shallow one.
#if defined(KB_TEST_GRAPH_SHADERC_PATH)
void RunGraphShaderDeepCacheRootCookTest() {
    const RenderMaterialGraphShaderSource shader = CompileConstantColorGraph("0.15 0.65 0.25 1");
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };

    std::filesystem::path cacheRoot = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "deep_project_root";
    while (cacheRoot.string().size() < 200U) {
        cacheRoot /= "user_project_folder_nested_one_level";
    }
    std::error_code error;
    std::filesystem::remove_all(kb::platform::ExtendedLengthPath(cacheRoot), error);

    RenderMaterialGraphShaderArtifactRequest request = MakeCookRequest("BaseOpaque");
    request.cacheRoot = cacheRoot.string();
    const RenderMaterialGraphShaderArtifactResult cooked =
        CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    std::string refusal;
    for (const RenderMaterialGraphDiagnostic& diagnostic : cooked.diagnostics) {
        if (diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error) {
            refusal += " [" + diagnostic.message + "]";
        }
    }
    const RenderMaterialGraphShaderBinary* binary = cooked.artifact.has_value()
        ? cooked.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)
        : nullptr;
    const std::string deepCookMessage =
        "A graph cook must publish its binary into a cache root past MAX_PATH:" + refusal;
    Require(cooked.Succeeded() && binary != nullptr && !binary->cacheHit, deepCookMessage.c_str());
    Require(binary->binaryPath.size() > 260U,
        "This test only proves anything while the cooked path is longer than MAX_PATH");
    // A path past MAX_PATH reaches the operating system the way the engine's own readers send it.
    const std::filesystem::path publishedBinary = kb::platform::ExtendedLengthPath(binary->binaryPath);
    Require(std::filesystem::is_regular_file(publishedBinary, error) &&
            std::filesystem::file_size(publishedBinary, error) == binary->byteSize,
        "A binary published into a deep cache root must be on disk whole");

    const RenderMaterialGraphShaderArtifactResult reused =
        CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    const RenderMaterialGraphShaderBinary* reusedBinary = reused.artifact.has_value()
        ? reused.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv)
        : nullptr;
    Require(reused.Succeeded() && reusedBinary != nullptr && reusedBinary->cacheHit,
        "A second cook must read the integrity marker back through the same deep path");

    const bool leftCompilerFile = std::ranges::any_of(
        std::filesystem::recursive_directory_iterator(kb::platform::ExtendedLengthPath(cacheRoot)),
        [](const std::filesystem::directory_entry& entry) {
            const std::string name = entry.path().filename().string();
            return entry.is_regular_file() &&
                (name.ends_with(".shaderc.tmp") || name.ends_with(".compile.tmp"));
        });
    Require(!leftCompilerFile,
        "The shader compiler must not leave its working files in a project's cache");

    // ... nor in the shared scratch root, where every cook of every process would pile them up.
    // A probe name carries this process's id the way the compiler's working files do.
    const std::filesystem::path probe = kb::platform::ToolScratchPath("probe", error);
    const std::string probeName = probe.filename().string();
    const std::string processTag = probeName.substr(0U, probeName.rfind('.') + 1U).substr(std::string{ ".kb-probe" }.size());
    const bool leftScratchFile = std::ranges::any_of(
        std::filesystem::directory_iterator(probe.parent_path()),
        [&processTag](const std::filesystem::directory_entry& entry) {
            const std::string name = entry.path().filename().string();
            return name.starts_with(".kb-compile.tmp" + processTag) || name.starts_with(".kb-shaderc.tmp" + processTag);
        });
    Require(!error && !leftScratchFile,
        "The shader compiler must not leave its working files in the tool scratch directory");
    std::filesystem::remove_all(kb::platform::ExtendedLengthPath(cacheRoot), error);
}

#endif

#if defined(KB_TEST_GRAPH_SHADERC_PATH)
// The Unreal/Unity utility nodes added after the first sweep, cooked for every graph pass rather than only
// BaseOpaque: the shadow, G-buffer and motion-vector wrappers each compile the node in their own context.
[[nodiscard]] bool SweepsEveryPass(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::WorldAlignedTexture:
    case RenderMaterialGraphNodeKind::FlipBook:
    case RenderMaterialGraphNodeKind::Checkerboard:
    case RenderMaterialGraphNodeKind::PolarCoordinates:
    case RenderMaterialGraphNodeKind::Twirl:
    case RenderMaterialGraphNodeKind::Ellipse:
    case RenderMaterialGraphNodeKind::Rectangle:
    case RenderMaterialGraphNodeKind::RoundedRectangle:
    case RenderMaterialGraphNodeKind::BlendOverlay:
    case RenderMaterialGraphNodeKind::BlendScreen:
    case RenderMaterialGraphNodeKind::BlendSoftLight:
    case RenderMaterialGraphNodeKind::BlendHardLight:
    case RenderMaterialGraphNodeKind::BlendLinearLight:
    case RenderMaterialGraphNodeKind::BlendColorBurn:
    case RenderMaterialGraphNodeKind::BlendColorDodge:
    case RenderMaterialGraphNodeKind::BlendDarken:
    case RenderMaterialGraphNodeKind::BlendLighten:
    case RenderMaterialGraphNodeKind::BlendDifference:
    case RenderMaterialGraphNodeKind::Contrast:
    case RenderMaterialGraphNodeKind::HueShift:
    case RenderMaterialGraphNodeKind::ChannelMixer:
    case RenderMaterialGraphNodeKind::RemapValueRange:
    case RenderMaterialGraphNodeKind::Noise:
        return true;
    default:
        return false;
    }
}

// The settings a kind is swept with: the default hint, plus every Noise function and Voronoi quality.
[[nodiscard]] std::vector<std::string> SweepHints(RenderMaterialGraphNodeKind kind) {
    if (kind != RenderMaterialGraphNodeKind::Noise) {
        return { std::string{} };
    }
    return {
        std::string{},
        "gradient true 1 1 6 2 -1 1",
        "value false 1 1 6 2 -1 1",
        "voronoi true 1 1 6 2 -1 1",
        "voronoi true 1 2 3 2 0 1",
        "voronoi false 2 3 2 2 0 1",
        "voronoi false 2 4 1 2 0 1",
        "simplex false 4 1 10 3 0 1",
    };
}

// The negator's sweep: every node kind alone, each of its outputs routed into the Material
// Output, compiled to shader source and cooked by shaderc (BaseOpaque, SPIR-V). Unconnected
// inputs take the node's defaults, as in the editor; a texture input gets a Texture Object.
void RunGraphShaderEveryNodeOutputCooksTest(std::uint32_t shard, std::uint32_t shardCount) {
    Require(shardCount != 0U && shard < shardCount, "Shader node cook partition is invalid");
    std::size_t totalCases = 0U;
    std::size_t selectedCases = 0U;
    const auto makeRequest = [shard, shardCount](std::string pass) {
        auto request = MakeCookRequest(std::move(pass));
        request.cacheRoot = (std::filesystem::path{ request.cacheRoot } /
            ("node-sweep-" + std::to_string(shardCount) + "-" + std::to_string(shard))).generic_string();
        return request;
    };
    std::ostringstream failures;
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };
    for (const RenderMaterialGraphNodeKind kind : AllRenderMaterialGraphNodeKinds()) {
        switch (kind) {
        case RenderMaterialGraphNodeKind::MaterialOutput:
        case RenderMaterialGraphNodeKind::FunctionInput:
        case RenderMaterialGraphNodeKind::FunctionOutput:
        case RenderMaterialGraphNodeKind::MaterialFunctionCall:
        case RenderMaterialGraphNodeKind::LayerStack:
        case RenderMaterialGraphNodeKind::Reroute:
        case RenderMaterialGraphNodeKind::CompositeInput:
        case RenderMaterialGraphNodeKind::CompositeOutput:
        case RenderMaterialGraphNodeKind::CollectionParameter:
        case RenderMaterialGraphNodeKind::NamedRerouteUsage:
        case RenderMaterialGraphNodeKind::NamedRerouteDeclaration:
            continue;  // need a declaration, a function or a source of their own (the MAT-50 test)
        default:
            break;
        }
        const std::string kindName{ RenderMaterialGraphNodeKindName(kind) };
        for (const std::string& hint : SweepHints(kind))
        for (const std::string& outPin : RenderMaterialGraphNodeOutputPinNames(kind)) {
            const std::size_t caseIndex = totalCases++;
            if (caseIndex % shardCount != shard) continue;
            ++selectedCases;
            std::fprintf(stderr, "graph_shader_sweep case=%zu shard=%u/%u node=%s output=%s\n",
                caseIndex, shard, shardCount, kindName.c_str(), outPin.c_str());
            std::fflush(stderr);
            const RenderMaterialGraphPinType outType = RenderMaterialGraphPinDataType(kind, outPin, true);
            RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
            RenderMaterialGraphNode subject{ .id = 2U, .kind = kind };
            if (!hint.empty()) {
                subject.parameter.defaultValueHint = hint;
            }
            const bool textureOut = outType == RenderMaterialGraphPinType::Texture2D ||
                outType == RenderMaterialGraphPinType::TextureCube ||
                outType == RenderMaterialGraphPinType::Texture3D ||
                outType == RenderMaterialGraphPinType::Texture2DArray;
            if (textureOut) {
                subject.parameter.textureRole = "baseColor";
            }
            graph.nodes.push_back(subject);
            for (const std::string& inPin : RenderMaterialGraphNodeInputPinNames(kind)) {
                if (RenderMaterialGraphPinDataType(kind, inPin, false) == RenderMaterialGraphPinType::Texture2D) {
                    RenderMaterialGraphNode textureObject{ .id = 4U, .kind = RenderMaterialGraphNodeKind::TextureObject };
                    textureObject.parameter.textureRole = "baseColor";
                    graph.nodes.push_back(textureObject);
                    graph.links.push_back(MakeGraphLink(RenderMaterialGraphNodeKind::TextureObject, 4U, "texture", kind, 2U, inPin));
                }
            }
            const std::string sampleOut = RenderMaterialGraphNodeOutputPinNames(RenderMaterialGraphNodeKind::TextureSample).front();
            const auto sampleInto = [&](RenderMaterialGraphNodeKind sampler, const std::string& samplerIn, const std::string& samplerOut) {
                graph.nodes.push_back(RenderMaterialGraphNode{ .id = 3U, .kind = sampler });
                graph.links.push_back(MakeGraphLink(kind, 2U, outPin, sampler, 3U, samplerIn));
                graph.links.push_back(MakeGraphLink(sampler, 3U, samplerOut, RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
            };
            switch (outType) {
            case RenderMaterialGraphPinType::Texture2D:
                sampleInto(RenderMaterialGraphNodeKind::TextureSample, "texture", sampleOut);
                break;
            case RenderMaterialGraphPinType::TextureCube:
                sampleInto(RenderMaterialGraphNodeKind::TextureSampleCube, "texture", "color");
                break;
            case RenderMaterialGraphPinType::Texture3D:
                sampleInto(RenderMaterialGraphNodeKind::TextureSampleVolume, "texture", "color");
                break;
            case RenderMaterialGraphPinType::Texture2DArray:
                sampleInto(RenderMaterialGraphNodeKind::TextureSample2DArray, "texture", "color");
                break;
            case RenderMaterialGraphPinType::Float2:
                sampleInto(RenderMaterialGraphNodeKind::TextureSample, "uv", sampleOut);
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
            default:
                graph.links.push_back(MakeGraphLink(kind, 2U, outPin, RenderMaterialGraphNodeKind::MaterialOutput, 1U, "baseColor"));
                break;
            }
            const std::string where = kindName + (hint.empty() ? std::string{} : "[" + hint + "]") + "." + outPin;
            bool refused = false;
            for (const RenderMaterialGraphDiagnostic& diagnostic :
                 ValidateRenderMaterialGraphDocument(graph, RenderMaterialGraphRenderPath::GpuForward)) {
                if (!refused && diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error) {
                    failures << where << ": graph refused: " << diagnostic.message << "@@";
                    refused = true;
                }
            }
            if (refused) {
                continue;
            }
            const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(
                graph, RenderMaterialGraphBuildContext{ .assetId = 0xC000U + static_cast<std::uint32_t>(kind) });
            if (!compiled.Succeeded()) {
                failures << where << ": source: " << (compiled.diagnostics.empty() ? std::string{} : compiled.diagnostics.front().message) << "@@";
                continue;
            }
            const std::vector<std::string> passes = SweepsEveryPass(kind)
                ? std::vector<std::string>{ "BaseOpaque", "GBuffer", "ShadowDepth", std::string{ kRenderMaterialGraphMotionVectorsPass } }
                : std::vector<std::string>{ "BaseOpaque" };
            for (const std::string& pass : passes) {
                // The motion vector pass cooks only graphs that move or clip their surface: sweep it Masked.
                RenderMaterialGraphCompileResult masked;
                if (pass == kRenderMaterialGraphMotionVectorsPass) {
                    RenderMaterialGraphDocument maskedGraph = graph;
                    maskedGraph.blendMode = "masked";
                    masked = CompileRenderMaterialGraphToShaderSource(
                        maskedGraph, RenderMaterialGraphBuildContext{ .assetId = 0xC800U + static_cast<std::uint32_t>(kind) });
                    if (!masked.Succeeded()) {
                        failures << where << " (" << pass << "): source: " << (masked.diagnostics.empty() ? std::string{} : masked.diagnostics.front().message) << "@@";
                        continue;
                    }
                }
                const RenderMaterialGraphShaderArtifactResult cooked = CookRenderMaterialGraphShaderArtifact(
                    pass == kRenderMaterialGraphMotionVectorsPass ? masked.shader : compiled.shader, backends, makeRequest(pass));
                if (!cooked.Succeeded()) {
                    failures << where << " (" << pass << "): shaderc: "
                             << (cooked.diagnostics.empty() ? std::string{} : cooked.diagnostics.front().message.substr(cooked.diagnostics.front().message.size() > 700U ? cooked.diagnostics.front().message.size() - 700U : 0U)) << "@@";
                }
            }
            if (SweepsEveryPass(kind)) {
                // Every shader profile the engine ships: DXBC (-O3, warnings as errors), DXIL, GLSL, ESSL, Metal.
                const std::array<std::pair<kb::assets::bake::ShaderBakePlatform, std::vector<RenderMaterialGraphShaderBackend>>, 4U> profiles{ {
                    { kb::assets::bake::ShaderBakePlatform::Windows, { RenderMaterialGraphShaderBackend::Dxbc, RenderMaterialGraphShaderBackend::Dxil } },
                    { kb::assets::bake::ShaderBakePlatform::Linux, { RenderMaterialGraphShaderBackend::Glsl } },
                    { kb::assets::bake::ShaderBakePlatform::Android, { RenderMaterialGraphShaderBackend::Essl } },
                    { kb::assets::bake::ShaderBakePlatform::MacOS, { RenderMaterialGraphShaderBackend::Metal } },
                } };
                for (const auto& [platform, profileBackends] : profiles) {
                    RenderMaterialGraphShaderArtifactRequest request = makeRequest("BaseOpaque");
                    request.shaderPlatform = platform;
                    const RenderMaterialGraphShaderArtifactResult cooked =
                        CookRenderMaterialGraphShaderArtifact(compiled.shader, profileBackends, request);
                    if (!cooked.Succeeded()) {
                        failures << where << " (" << kb::assets::bake::ShaderBakePlatformName(platform) << "): shaderc: "
                                 << (cooked.diagnostics.empty() ? std::string{} : cooked.diagnostics.front().message.substr(cooked.diagnostics.front().message.size() > 700U ? cooked.diagnostics.front().message.size() - 700U : 0U)) << "@@";
                    }
                }
            }
        }
    }
    std::string report = failures.str();
    for (std::size_t at = report.find("@@"); at != std::string::npos; at = report.find("@@", at)) {
        report.replace(at, 2U, "\n");
    }
    if (!report.empty()) {
        std::fprintf(stderr, "%s", report.c_str());
    }
    Require(selectedCases != 0U, "Shader node cook partition did not exercise any output");
    Require(report.empty(), "KBMAT-MAT50: every node output must cook through shaderc (see the list above)");
    std::fprintf(stderr, "graph_shader_sweep passed shard=%u/%u selected=%zu total=%zu\n",
        shard, shardCount, selectedCases, totalCases);
    std::fflush(stderr);
}

#endif

void RunGraphShaderArtifactCacheTests() {
    const auto check = [](const char* name, void (*run)()) {
        std::fprintf(stderr, "graph_shader_tests begin %s\n", name);
        std::fflush(stderr);
        const auto started = std::chrono::steady_clock::now();
        run();
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        std::fprintf(stderr, "graph_shader_tests passed %s elapsed_ms=%.3f\n", name, milliseconds);
        std::fflush(stderr);
    };
    check("RunGraphShaderWrapperSourceTest", &RunGraphShaderWrapperSourceTest);
    check("RunGraphShaderBackendMetadataTest", &RunGraphShaderBackendMetadataTest);
    check("RunGraphShaderReflectionHashTest", &RunGraphShaderReflectionHashTest);
    check("RunGraphShaderManifestValidationTest", &RunGraphShaderManifestValidationTest);
    check("RunGraphShaderManifestRoundTripTest", &RunGraphShaderManifestRoundTripTest);
#if defined(KB_TEST_GRAPH_SHADERC_PATH)
    check("RunGraphShaderCookProducesBinaryTest", &RunGraphShaderCookProducesBinaryTest);
    check("RunGraphShaderVariantArtifactCoexistenceTest", &RunGraphShaderVariantArtifactCoexistenceTest);
    check("RunGraphShaderSameKeyConcurrentCookTest", &RunGraphShaderSameKeyConcurrentCookTest);
    check("RunGraphShadowDepthVertexAndAlphaCookTest", &RunGraphShadowDepthVertexAndAlphaCookTest);
    check("RunGraphShaderCookShadercFailureTest", &RunGraphShaderCookShadercFailureTest);
    check("RunGraphShaderArtifactDependencyInvalidationTest", &RunGraphShaderArtifactDependencyInvalidationTest);
    check("RunGraphShaderAutomaticIncludeInvalidationTest", &RunGraphShaderAutomaticIncludeInvalidationTest);
    check("RunGraphShaderToolchainIdentityInvalidationTest", &RunGraphShaderToolchainIdentityInvalidationTest);
    check("RunGraphShaderDeepCacheRootCookTest", &RunGraphShaderDeepCacheRootCookTest);
#endif
}

void RunGraphShaderNodeCookTests(std::uint32_t shard, std::uint32_t shardCount) {
#if defined(KB_TEST_GRAPH_SHADERC_PATH)
    RunGraphShaderEveryNodeOutputCooksTest(shard, shardCount);
#else
    static_cast<void>(shard);
    static_cast<void>(shardCount);
    Require(false, "Shader node cook validation requires the shader compiler");
#endif
}

void RunGraphShaderArtifactCookTests() {
    RunGraphShaderArtifactCacheTests();
#if defined(KB_TEST_GRAPH_SHADERC_PATH)
    RunGraphShaderNodeCookTests(0U, 1U);
#endif
}

} // namespace kb::render::tests
