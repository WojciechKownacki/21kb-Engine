#include "RendererTestSupport.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"

#include <algorithm>
#include <array>
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
    std::string_view shadingModel = "defaultLit") {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    graph.shadingModel = std::string{ shadingModel };
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
    Require(opaque.find("gl_FragColor = vec4(lighting + surface.emissive, surface.alpha);") != std::string::npos,
        "KBMAT-MAT08: Forward wrapper must combine lit color, emissive and surface alpha");
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
            gbuffer.find("gl_FragData[0] = vec4(surface.baseColor.rgb, 1.0);") != std::string::npos &&
            gbuffer.find("gl_FragData[1] = vec4(worldNormal * 0.5 + 0.5, 1.0);") != std::string::npos &&
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

void RunGraphShaderArtifactCookTests() {
    RunGraphShaderWrapperSourceTest();
    RunGraphShaderBackendMetadataTest();
    RunGraphShaderReflectionHashTest();
    RunGraphShaderManifestValidationTest();
    RunGraphShaderManifestRoundTripTest();
#if defined(KB_TEST_GRAPH_SHADERC_PATH)
    RunGraphShaderCookProducesBinaryTest();
    RunGraphShaderVariantArtifactCoexistenceTest();
    RunGraphShaderSameKeyConcurrentCookTest();
    RunGraphShadowDepthVertexAndAlphaCookTest();
    RunGraphShaderCookShadercFailureTest();
    RunGraphShaderArtifactDependencyInvalidationTest();
    RunGraphShaderAutomaticIncludeInvalidationTest();
    RunGraphShaderToolchainIdentityInvalidationTest();
#endif
}

} // namespace kb::render::tests
