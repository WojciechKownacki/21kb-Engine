#include "RendererTestSupport.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
#include "kb/render/resources/RenderMaterialGraphProgramBindingBuilder.hpp"
#include "kb/render/resources/RenderMaterialGraphShaderArtifact.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/scene/MeshPassType.hpp"
#include "kb/render/scene/cache/SceneCachedDrawCommand.hpp"
#include "../src/scene/submit/SceneMeshPassResources.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace kb::render::tests {
namespace {

[[nodiscard]] bool InitHeadlessNoopBgfx() {
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    init.resolution.width = 64U;
    init.resolution.height = 64U;
    init.resolution.reset = BGFX_RESET_NONE;
    return bgfx::init(init);
}

[[nodiscard]] RenderMaterialResource MakeGraphMaterialResource(
    std::uint64_t graphSourceHash,
    std::uint64_t variantKey = 0xA66A'0000'0000'0001ULL,
    std::uint64_t pipelineStateKey = 0xA66A'0000'0000'0002ULL,
    std::uint64_t materialTypeId = 0xC0DEU,
    std::uint32_t materialTypeVersion = 1U,
    bool requiresGeneratedVertexShader = false) {
    RenderMaterialResource material{};
    material.graphProgram.active = true;
    material.graphProgram.materialTypeId = materialTypeId;
    material.graphProgram.materialTypeVersion = materialTypeVersion;
    material.graphProgram.graphSourceHash = graphSourceHash;
    material.graphProgram.variantKey = variantKey;
    material.graphProgram.pipelineStateKey = pipelineStateKey;
    material.graphProgram.requiresGeneratedVertexShader = requiresGeneratedVertexShader;
    return material;
}

#if defined(KB_TEST_GRAPH_SHADERC_PATH)
[[nodiscard]] RenderMaterialGraphShaderSource CompileConstantColorGraphForSelection(std::string_view colorHint) {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode constant{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 80,
    };
    constant.parameter.defaultValueHint = std::string{ colorHint };
    graph.nodes.push_back(constant);
    RenderMaterialGraphLink link{
        .fromNodeId = 2U,
        .fromPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::ConstantColor, "rgba", true),
        .fromPin = "rgba",
        .toNodeId = 1U,
        .toPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
        .toPin = "baseColor",
    };
    link.id = MakeRenderMaterialGraphLinkId(link);
    graph.links.push_back(link);
    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x0700U });
    Require(compiled.Succeeded(), "KBMAT-MAT07: Selection graph must compile");
    return compiled.shader;
}

[[nodiscard]] RenderMaterialGraphShaderSource CompileWorldPositionOffsetGraphForSelection() {
    RenderMaterialGraphDocument graph = MakeDefaultRenderMaterialGraphDocument();
    RenderMaterialGraphNode color{
        .id = 2U,
        .kind = RenderMaterialGraphNodeKind::ConstantColor,
        .positionX = 80,
        .positionY = 80,
    };
    color.parameter.defaultValueHint = "0.2 0.4 0.6 1";
    RenderMaterialGraphNode offset{
        .id = 3U,
        .kind = RenderMaterialGraphNodeKind::ConstantVector,
        .positionX = 80,
        .positionY = 220,
    };
    offset.parameter.defaultValueHint = "0.35 0 0";
    graph.nodes.push_back(color);
    graph.nodes.push_back(offset);

    RenderMaterialGraphLink colorLink{
        .fromNodeId = 2U,
        .fromPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::ConstantColor, "rgba", true),
        .fromPin = "rgba",
        .toNodeId = 1U,
        .toPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "baseColor", false),
        .toPin = "baseColor",
    };
    colorLink.id = MakeRenderMaterialGraphLinkId(colorLink);
    graph.links.push_back(colorLink);

    RenderMaterialGraphLink offsetLink{
        .fromNodeId = 3U,
        .fromPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::ConstantVector, "xyz", true),
        .fromPin = "xyz",
        .toNodeId = 1U,
        .toPinId = RenderMaterialGraphStablePinId(RenderMaterialGraphNodeKind::MaterialOutput, "worldPositionOffset", false),
        .toPin = "worldPositionOffset",
    };
    offsetLink.id = MakeRenderMaterialGraphLinkId(offsetLink);
    graph.links.push_back(offsetLink);

    const RenderMaterialGraphCompileResult compiled = CompileRenderMaterialGraphToShaderSource(graph, RenderMaterialGraphBuildContext{ .assetId = 0x1900U });
    Require(compiled.Succeeded() && compiled.shader.reflection.hasWorldPositionOffset,
        "KBMAT-MAT99-19: WPO graph must compile and flag the generated vertex shader requirement");
    return compiled.shader;
}

[[nodiscard]] bool CookGraphForActiveBackend(
    const RenderMaterialGraphShaderSource& shader,
    const std::string& cacheRoot,
    std::string_view pass = "BaseOpaque") {
    RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
    request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
    request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
    request.cacheRoot = cacheRoot;
    request.pass = pass;
    const std::array<RenderMaterialGraphShaderBackend, 1U> backends{ RenderMaterialGraphShaderBackend::Dxbc };
    const RenderMaterialGraphShaderArtifactResult result = CookRenderMaterialGraphShaderArtifact(shader, backends, request);
    return result.Succeeded();
}
#endif

void RunSceneMeshPassProgramSelectionTest() {
    if (!InitHeadlessNoopBgfx()) {
        Require(false, "KBMAT-MAT07: Headless Noop bgfx initialization failed");
        return;
    }

    {
        SceneMeshPassResources passResources;
        Require(passResources.Initialize(), "KBMAT-MAT07: Pass resources must initialize the builtin programs under headless Noop");
        passResources.ResetProgramBindStats();

        const RenderMaterialResource builtinMaterial{};

        // Builtin PBR material keeps using the static builtin program (no graph program).
        const SceneMeshPassProgramResolution builtinResolution = passResources.ResolveMeshPassProgram(&builtinMaterial, MeshPassType::BaseOpaque);
        Require(bgfx::isValid(builtinResolution.program) && !builtinResolution.graphProgram && !builtinResolution.fellBackToBuiltin,
            "KBMAT-MAT07: Builtin material must resolve to the static builtin program");

        // Selection pass is an explicit special case bound to the selection program.
        const SceneMeshPassProgramResolution selectionResolution = passResources.ResolveMeshPassProgram(&builtinMaterial, MeshPassType::SelectionId);
        Require(bgfx::isValid(selectionResolution.program) && !selectionResolution.graphProgram,
            "KBMAT-MAT07: Selection pass must resolve to the dedicated selection program");
        Require(selectionResolution.program.idx != builtinResolution.program.idx,
            "KBMAT-MAT07: Selection program must differ from the opaque program");

        // Graph material with no cooked binary must safely fall back to the builtin program.
        const RenderMaterialResource graphFallback = MakeGraphMaterialResource(0xDEAD1234U);
        const SceneMeshPassProgramResolution fallbackResolution = passResources.ResolveMeshPassProgram(&graphFallback, MeshPassType::BaseOpaque);
        Require(bgfx::isValid(fallbackResolution.program) && !fallbackResolution.graphProgram && fallbackResolution.fellBackToBuiltin,
            "KBMAT-MAT07: A graph material without a cooked program must fall back to the builtin program");
        Require(fallbackResolution.status == SceneRenderMaterialProgramStatus::GraphFallback &&
                fallbackResolution.key.graphSourceHash == graphFallback.graphProgram.graphSourceHash &&
                fallbackResolution.key.variantKey == graphFallback.graphProgram.variantKey &&
                fallbackResolution.key.pipelineStateKey == graphFallback.graphProgram.pipelineStateKey &&
                fallbackResolution.materialProgramIdentity != 0U,
            "KBMAT-MAT66: Graph fallback diagnostics must preserve material id/hash/variant/pipeline identity");
        Require(fallbackResolution.program.idx == builtinResolution.program.idx,
            "KBMAT-MAT07: The graph fallback must reuse the builtin opaque program");
        Require(passResources.ProgramBindStats().builtinFallbackBindCount == 1U,
            "KBMAT-MAT07: Builtin fallback program usage must be counted in submit stats");
        const SceneMeshPassProgramResolution missingGBufferResolution = passResources.ResolveMeshPassProgram(&graphFallback, MeshPassType::GBuffer);
        Require(!bgfx::isValid(missingGBufferResolution.program) && !missingGBufferResolution.fellBackToBuiltin,
            "Deferred graph GBuffer pass must fail closed when the GBuffer artifact is missing");
        Require(passResources.ProgramBindStats().builtinFallbackBindCount == 1U,
            "Deferred graph GBuffer miss must not increment builtin fallback usage");
        Require(passResources.ProgramBindStats().programSwitchCount >= 2U,
            "KBMAT-MAT07: Program switch stats must count distinct bound programs");

        RenderResourceRegistry emptyResources;
        SceneRenderResourceMap emptyResourceMap;
        PackedSceneLighting emptyLighting{};
        const std::array<float, 4U> zero4{};
        MeshDrawCommand fallbackCommand{};
        fallbackCommand.materialResource = &graphFallback;
        fallbackCommand.materialAssetId = 0xDEAD1234U;
        fallbackCommand.meshAssetId = 0x7777U;
        const bgfx::ProgramHandle boundFallback = passResources.Bind(SceneMeshPassBindDesc{
            .command = fallbackCommand,
            .resources = emptyResources,
            .resourceMap = emptyResourceMap,
            .pass = MeshPassType::BaseOpaque,
            .lighting = emptyLighting,
            .cameraPosition = zero4,
            .frameTime = zero4,
            .dynamicParameter = zero4,
        });
        const SceneMeshPassProgramResolution lastFallback = passResources.LastProgramResolution();
        Require(boundFallback.idx == fallbackResolution.program.idx &&
                lastFallback.status == SceneRenderMaterialProgramStatus::GraphFallback &&
                lastFallback.materialProgramIdentity == fallbackResolution.materialProgramIdentity &&
                lastFallback.key.graphSourceHash == graphFallback.graphProgram.graphSourceHash &&
                lastFallback.key.variantKey == graphFallback.graphProgram.variantKey,
            "KBMAT-MAT66: Runtime bind diagnostics must preserve material/hash/variant/program status after fallback");

#if defined(KB_TEST_GRAPH_SHADERC_PATH)
        const std::filesystem::path cacheRoot = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat07_program_selection";
        std::error_code error;
        std::filesystem::remove_all(cacheRoot, error);

        const RenderMaterialGraphShaderSource shaderA = CompileConstantColorGraphForSelection("0.2 0.4 0.6 1");
        const RenderMaterialGraphShaderSource shaderB = CompileConstantColorGraphForSelection("0.9 0.1 0.3 1");
        Require(shaderA.sourceHash != shaderB.sourceHash, "KBMAT-MAT07: Distinct graphs must have distinct source hashes");
        Require(CookGraphForActiveBackend(shaderA, cacheRoot.generic_string()), "KBMAT-MAT07: Graph A must cook a DXBC binary");
        Require(CookGraphForActiveBackend(shaderB, cacheRoot.generic_string()), "KBMAT-MAT07: Graph B must cook a DXBC binary");
        Require(CookGraphForActiveBackend(shaderA, cacheRoot.generic_string(), "GBuffer"),
            "Deferred graph material must cook a distinct GBuffer DXBC binary");

        passResources.SetGraphShaderCacheRoot(cacheRoot.generic_string());
        passResources.ResetProgramBindStats();

        const std::uint64_t variantA = RenderMaterialGraphVariantKey(shaderA);
        const std::uint64_t variantB = RenderMaterialGraphVariantKey(shaderB);
        const std::uint64_t pipelineA = RenderMaterialGraphPipelineStateKey(shaderA);
        const std::uint64_t pipelineB = RenderMaterialGraphPipelineStateKey(shaderB);
        const RenderMaterialResource graphMaterialA = MakeGraphMaterialResource(shaderA.sourceHash, variantA, pipelineA);
        const RenderMaterialResource graphMaterialB = MakeGraphMaterialResource(shaderB.sourceHash, variantB, pipelineB);

        const SceneMeshPassProgramResolution graphA = passResources.ResolveMeshPassProgram(&graphMaterialA, MeshPassType::BaseOpaque);
        Require(graphA.graphProgram && bgfx::isValid(graphA.program) && !graphA.fellBackToBuiltin,
            "KBMAT-MAT07: A cooked graph material must bind its own GPU graph program");
        Require(graphA.status == SceneRenderMaterialProgramStatus::GraphReady &&
                graphA.key.graphSourceHash == shaderA.sourceHash &&
                graphA.key.variantKey == variantA &&
                graphA.key.pipelineStateKey == pipelineA &&
                graphA.key.materialTypeId == graphMaterialA.graphProgram.materialTypeId &&
                graphA.key.materialTypeVersion == graphMaterialA.graphProgram.materialTypeVersion,
            "KBMAT-MAT66: Successful graph program resolution must expose full runtime program diagnostics");
        Require(passResources.ProgramBindStats().builtinFallbackBindCount == 0U,
            "Deferred graph setup must not count builtin fallback before GBuffer resolution");

        const SceneMeshPassProgramResolution graphGBuffer = passResources.ResolveMeshPassProgram(&graphMaterialA, MeshPassType::GBuffer);
        Require(graphGBuffer.graphProgram && bgfx::isValid(graphGBuffer.program) && !graphGBuffer.fellBackToBuiltin &&
                graphGBuffer.status == SceneRenderMaterialProgramStatus::GraphReady &&
                graphGBuffer.key.graphSourceHash == shaderA.sourceHash,
            "Deferred graph GBuffer pass must bind the cooked GBuffer graph program without builtin fallback");
        Require(passResources.ProgramBindStats().builtinFallbackBindCount == 0U,
            "Deferred graph GBuffer program resolution must keep builtinFallbackBindCount at zero");

        const SceneMeshPassProgramResolution graphB = passResources.ResolveMeshPassProgram(&graphMaterialB, MeshPassType::BaseOpaque);
        Require(graphB.graphProgram && bgfx::isValid(graphB.program),
            "KBMAT-MAT07: A second cooked graph material must bind its own GPU graph program");
        Require(graphA.program.idx != graphB.program.idx,
            "KBMAT-MAT07: Two graph materials with different graph source hashes must bind two different programs");

        const SceneMeshPassProgramResolution graphAReuse = passResources.ResolveMeshPassProgram(&graphMaterialA, MeshPassType::BaseOpaque);
        Require(graphAReuse.graphProgram && graphAReuse.program.idx == graphA.program.idx,
            "KBMAT-MAT07: Two materials sharing a graph program key must reuse the same program");

        MaterialProgramRegistryStats registryStatsBefore = passResources.ProgramRegistryStats();
        const RenderMaterialResource graphMaterialVariant = MakeGraphMaterialResource(shaderA.sourceHash, variantA ^ 0x8000'0000'0000'0000ULL, pipelineA);
        const SceneMeshPassProgramResolution graphVariant = passResources.ResolveMeshPassProgram(&graphMaterialVariant, MeshPassType::BaseOpaque);
        Require(graphVariant.graphProgram &&
                graphVariant.key.variantKey != graphA.key.variantKey &&
                graphVariant.materialProgramIdentity != graphA.materialProgramIdentity &&
                passResources.ProgramRegistryStats().loads == registryStatsBefore.loads + 1U,
            "KBMAT-MAT66: Hot-reloaded static variant with the same source binary path must not reuse the stale program binding");

        registryStatsBefore = passResources.ProgramRegistryStats();
        const RenderMaterialResource graphMaterialPipeline = MakeGraphMaterialResource(shaderA.sourceHash, variantA, pipelineA ^ 0x4000'0000'0000'0000ULL);
        const SceneMeshPassProgramResolution graphPipeline = passResources.ResolveMeshPassProgram(&graphMaterialPipeline, MeshPassType::BaseOpaque);
        Require(graphPipeline.graphProgram &&
                graphPipeline.key.pipelineStateKey != graphA.key.pipelineStateKey &&
                graphPipeline.materialProgramIdentity != graphA.materialProgramIdentity &&
                passResources.ProgramRegistryStats().loads == registryStatsBefore.loads + 1U,
            "KBMAT-MAT66: Pipeline-state identity changes must acquire a distinct graph program key");

        registryStatsBefore = passResources.ProgramRegistryStats();
        const RenderMaterialResource graphMaterialType = MakeGraphMaterialResource(shaderA.sourceHash, variantA, pipelineA, 0xC0DE'0000'0000'0001ULL, 2U);
        const SceneMeshPassProgramResolution graphType = passResources.ResolveMeshPassProgram(&graphMaterialType, MeshPassType::BaseOpaque);
        Require(graphType.graphProgram &&
                graphType.key.materialTypeId != graphA.key.materialTypeId &&
                graphType.key.materialTypeVersion != graphA.key.materialTypeVersion &&
                graphType.materialProgramIdentity != graphA.materialProgramIdentity &&
                passResources.ProgramRegistryStats().loads == registryStatsBefore.loads + 1U,
            "KBMAT-MAT66: Material type id/version changes must invalidate the graph program binding");

        const RenderMaterialGraphShaderSource wpoShader = CompileWorldPositionOffsetGraphForSelection();
        Require(CookGraphForActiveBackend(wpoShader, cacheRoot.generic_string()),
            "KBMAT-MAT99-19: WPO graph must cook fragment and generated vertex binaries for scene selection");
        const std::uint64_t wpoVariant = RenderMaterialGraphVariantKey(wpoShader);
        const std::uint64_t wpoPipeline = RenderMaterialGraphPipelineStateKey(wpoShader);
        const RenderMaterialResource wpoMaterial = MakeGraphMaterialResource(
            wpoShader.sourceHash,
            wpoVariant,
            wpoPipeline,
            0xC0DEU,
            1U,
            true);
        const SceneMeshPassProgramResolution wpoReady = passResources.ResolveMeshPassProgram(&wpoMaterial, MeshPassType::BaseOpaque);
        Require(wpoReady.graphProgram && bgfx::isValid(wpoReady.program) && !wpoReady.fellBackToBuiltin &&
                wpoReady.key.requiresGeneratedVertexShader,
            "KBMAT-MAT99-19: WPO graph material must bind the generated scene vertex program when vs.bin exists");

        const std::filesystem::path wpoVertexPath = cacheRoot /
            ("graph_" + std::to_string(wpoShader.sourceHash)) /
            "BaseOpaque" /
            "dxbc" /
            "vs.bin";
        Require(std::filesystem::exists(wpoVertexPath), "KBMAT-MAT99-19: WPO cook must leave a scene vs.bin on disk");
        std::filesystem::remove(wpoVertexPath, error);
        Require(!error, "KBMAT-MAT99-19: WPO test could not remove generated vs.bin");
        registryStatsBefore = passResources.ProgramRegistryStats();
        const RenderMaterialResource missingWpoVsMaterial = MakeGraphMaterialResource(
            wpoShader.sourceHash,
            wpoVariant ^ 0x0100'0000'0000'0000ULL,
            wpoPipeline,
            0xC0DEU,
            1U,
            true);
        const SceneMeshPassProgramResolution missingWpoVs = passResources.ResolveMeshPassProgram(&missingWpoVsMaterial, MeshPassType::BaseOpaque);
        Require(!missingWpoVs.graphProgram && missingWpoVs.fellBackToBuiltin &&
                missingWpoVs.status == SceneRenderMaterialProgramStatus::GraphFallback &&
                missingWpoVs.key.requiresGeneratedVertexShader &&
                passResources.ProgramRegistryStats().failures == registryStatsBefore.failures + 1U,
            "KBMAT-MAT99-19: missing WPO vs.bin must not silently bind the fixed mesh vertex shader as a graph program");

        Require(passResources.ProgramBindStats().graphProgramBindCount == 8U,
            "KBMAT-MAT07: Graph program binds must be counted in submit stats");
#endif

        passResources.Shutdown();
    }

    bgfx::shutdown();
}

void RunSceneMeshDrawCommandKeyProgramInvalidationTest() {
    const SceneCachedDrawCommandKeyHash hasher;

    SceneCachedDrawCommandKey baseKey{};
    baseKey.pass = MeshPassType::BaseOpaque;
    baseKey.materialAssetId = 0x42U;
    baseKey.materialHandleValue = 7U;
    baseKey.materialResourceVersion = 3U;
    baseKey.materialProgramKey = 0x1111U;
    baseKey.materialProgramTypeId = 0x1000'0000'0000'0001ULL;
    baseKey.materialProgramTypeVersion = 3U;
    baseKey.materialProgramGraphSourceHash = 0x2000'0000'0000'0002ULL;
    baseKey.materialProgramVariantKey = 0x3000'0000'0000'0003ULL;
    baseKey.materialProgramPipelineStateKey = 0x4000'0000'0000'0004ULL;
    baseKey.materialGraphProgram = true;

    SceneCachedDrawCommandKey changedProgram = baseKey;
    changedProgram.materialProgramKey = 0x2222U;

    Require(!(baseKey == changedProgram),
        "KBMAT-MAT07: A graph program key change must produce a different draw command cache key");
    Require(hasher(baseKey) != hasher(changedProgram),
        "KBMAT-MAT07: A graph program key change must invalidate the draw command cache hash");

    SceneCachedDrawCommandKey changedVariant = baseKey;
    changedVariant.materialProgramVariantKey ^= 0x8000'0000'0000'0000ULL;
    Require(!(baseKey == changedVariant),
        "KBMAT-MAT66: A graph variant key change must invalidate the draw command cache key without truncation");
    Require(hasher(baseKey) != hasher(changedVariant),
        "KBMAT-MAT66: A graph variant key change must invalidate the draw command cache hash");

    SceneCachedDrawCommandKey changedPipeline = baseKey;
    changedPipeline.materialProgramPipelineStateKey ^= 0x4000'0000'0000'0000ULL;
    Require(!(baseKey == changedPipeline),
        "KBMAT-MAT66: A graph pipeline-state key change must invalidate the draw command cache key");

    SceneCachedDrawCommandKey sameKey = baseKey;
    Require(sameKey == baseKey && hasher(sameKey) == hasher(baseKey),
        "KBMAT-MAT07: An unchanged graph program key must reuse the cached draw command");
}

} // namespace

void RunSceneMeshPassProgramSelectionTests() {
    RunSceneMeshDrawCommandKeyProgramInvalidationTest();
    RunSceneMeshPassProgramSelectionTest();
}

} // namespace kb::render::tests
