#include "RendererTestSupport.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

[[nodiscard]] RenderMaterialGraphShaderSource CompileConstantColorGraph(std::string_view colorHint) {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
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
    Require(opaque.find("ctx.twoSidedSign = gl_FrontFacing ? 1.0 : -1.0;") != std::string::npos,
        "KBMAT-MAT46: Forward wrapper must expose a real front/back-face sign to graph nodes");
    Require(opaque.find("gl_FragColor = vec4(lighting + surface.emissive, surface.alpha);") != std::string::npos,
        "KBMAT-MAT08: Forward wrapper must combine lit color, emissive and surface alpha");

    const std::string shadow = BuildGraphFragmentWrapperSource(shader, "ShadowDepth");
    Require(shadow.find("gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);") != std::string::npos,
        "KBMAT-MAT04: ShadowDepth wrapper must not depend on full surface shading output");
    Require(shadow.find("surface.alphaClipThreshold") != std::string::npos,
        "KBMAT-MAT04: ShadowDepth wrapper must still honor the graph alpha clip contract");
    Require(opaque != shadow, "KBMAT-MAT04: Different passes must produce different wrapper sources");
}

void RunGraphShaderBackendMetadataTest() {
    Require(RenderMaterialGraphShaderBackendProfile(RenderMaterialGraphShaderBackend::Spirv) == std::string_view{ "spirv" },
        "KBMAT-MAT04: SPIR-V backend must map to the spirv shaderc profile");
    Require(RenderMaterialGraphShaderBackendPlatform(RenderMaterialGraphShaderBackend::Dxbc) == std::string_view{ "windows" },
        "KBMAT-MAT04: DXBC backend must compile against the windows platform");
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
    return request;
}

void RunGraphShaderCookProducesBinaryTest() {
    const RenderMaterialGraphShaderSource shader = CompileConstantColorGraph("0.2 0.4 0.6 1");
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };

    std::error_code error;
    std::filesystem::remove_all(std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / ("graph_" + std::to_string(shader.sourceHash)), error);

    const RenderMaterialGraphShaderArtifactResult first = CookRenderMaterialGraphShaderArtifact(shader, backends, MakeCookRequest("BaseOpaque"));
    Require(first.Succeeded(), "KBMAT-MAT04: Cooking a simple graph material must succeed");
    Require(first.artifact.has_value(), "KBMAT-MAT04: Cook must produce an artifact");
    const RenderMaterialGraphShaderBinary* spirv = first.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv);
    Require(spirv != nullptr, "KBMAT-MAT04: Cook must produce a SPIR-V graph fragment binary");
    Require(!spirv->cacheHit, "KBMAT-MAT04: First cook of a fresh graph must be a real shaderc compile, not a cache hit");
    Require(std::filesystem::exists(spirv->binaryPath, error) && std::filesystem::file_size(spirv->binaryPath, error) > 0U,
        "KBMAT-MAT04: Cooked graph fragment binary must exist on disk in the staging cache");

    const std::array<RenderMaterialGraphShaderArtifact, 1U> manifestArtifacts{ *first.artifact };
    const RenderMaterialGraphShaderManifest manifest = BuildRenderMaterialGraphShaderManifest(manifestArtifacts);
    Require(ValidateRenderMaterialGraphShaderManifest(manifest).empty(),
        "KBMAT-MAT04: A freshly cooked manifest must validate against on-disk binaries");

    const RenderMaterialGraphShaderArtifactResult second = CookRenderMaterialGraphShaderArtifact(shader, backends, MakeCookRequest("BaseOpaque"));
    Require(second.Succeeded() && second.artifact.has_value(), "KBMAT-MAT04: Rebuilding an unchanged graph must succeed");
    const RenderMaterialGraphShaderBinary* cachedSpirv = second.artifact->FindBinary(RenderMaterialGraphShaderBackend::Spirv);
    Require(cachedSpirv != nullptr && cachedSpirv->cacheHit,
        "KBMAT-MAT04: Rebuilding an unchanged graph must hit the shader binary cache");
}

void RunGraphShaderCookShadercFailureTest() {
    RenderMaterialGraphShaderSource broken{};
    broken.entryPoint = "EvaluateMaterialGraph";
    broken.sourceHash = 0xDEADBEEFU;
    broken.source = "this is definitely not valid shader code !!!\n";

    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Spirv };
    std::error_code error;
    std::filesystem::remove_all(std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / ("graph_" + std::to_string(broken.sourceHash)), error);

    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(broken, backends, MakeCookRequest("BaseOpaque"));
    Require(!result.Succeeded(), "KBMAT-MAT04: A graph that fails shaderc compilation must not report success");
    bool hasError = false;
    for (const RenderMaterialGraphDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error) {
            hasError = true;
        }
    }
    Require(hasError, "KBMAT-MAT04: shaderc compilation failure must produce an error diagnostic");
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
#endif

void RunGraphShaderManifestRoundTripTest() {
    RenderMaterialGraphShaderArtifact artifactA{};
    artifactA.graphSourceHash = 0x1111U;
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

    const RenderMaterialGraphShaderManifestEntry* dxbc = parsed.Find(0x1111U, "BaseOpaque", RenderMaterialGraphShaderBackend::Dxbc);
    Require(dxbc != nullptr, "KBMAT-MAT13: Round-tripped manifest must remain queryable by graph hash/pass/backend");
    Require(dxbc->wrapperHash == 0x2222U && dxbc->reflectionHash == 0x3333U && dxbc->dependencyHash == 0x4444U &&
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
    RunGraphShaderCookShadercFailureTest();
    RunGraphShaderArtifactDependencyInvalidationTest();
#endif
}

} // namespace kb::render::tests
