#include "RendererTestSupport.hpp"

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"
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

[[nodiscard]] RenderMaterialResource MakeGraphMaterialResource(std::uint64_t graphSourceHash) {
    RenderMaterialResource material{};
    material.graphProgram.active = true;
    material.graphProgram.materialTypeId = 0xC0DEU;
    material.graphProgram.materialTypeVersion = 1U;
    material.graphProgram.graphSourceHash = graphSourceHash;
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

[[nodiscard]] bool CookGraphForActiveBackend(const RenderMaterialGraphShaderSource& shader, const std::string& cacheRoot) {
    RenderMaterialGraphShaderArtifactRequest request{};
    request.shadercPath = KB_TEST_GRAPH_SHADERC_PATH;
    request.varyingDefPath = KB_TEST_GRAPH_SHADER_VARYING_DEF;
    request.includeDirs = { KB_TEST_GRAPH_SHADER_INCLUDE_DIR, KB_TEST_GRAPH_BGFX_SHADER_INCLUDE_DIR };
    request.cacheRoot = cacheRoot;
    request.pass = "BaseOpaque";
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
        Require(fallbackResolution.program.idx == builtinResolution.program.idx,
            "KBMAT-MAT07: The graph fallback must reuse the builtin opaque program");
        Require(passResources.ProgramBindStats().builtinFallbackBindCount == 1U,
            "KBMAT-MAT07: Builtin fallback program usage must be counted in submit stats");
        Require(passResources.ProgramBindStats().programSwitchCount >= 2U,
            "KBMAT-MAT07: Program switch stats must count distinct bound programs");

#if defined(KB_TEST_GRAPH_SHADERC_PATH)
        const std::filesystem::path cacheRoot = std::filesystem::path{ KB_TEST_GRAPH_SHADER_CACHE_DIR } / "mat07_program_selection";
        std::error_code error;
        std::filesystem::remove_all(cacheRoot, error);

        const RenderMaterialGraphShaderSource shaderA = CompileConstantColorGraphForSelection("0.2 0.4 0.6 1");
        const RenderMaterialGraphShaderSource shaderB = CompileConstantColorGraphForSelection("0.9 0.1 0.3 1");
        Require(shaderA.sourceHash != shaderB.sourceHash, "KBMAT-MAT07: Distinct graphs must have distinct source hashes");
        Require(CookGraphForActiveBackend(shaderA, cacheRoot.generic_string()), "KBMAT-MAT07: Graph A must cook a DXBC binary");
        Require(CookGraphForActiveBackend(shaderB, cacheRoot.generic_string()), "KBMAT-MAT07: Graph B must cook a DXBC binary");

        passResources.SetGraphShaderCacheRoot(cacheRoot.generic_string());
        passResources.ResetProgramBindStats();

        const RenderMaterialResource graphMaterialA = MakeGraphMaterialResource(shaderA.sourceHash);
        const RenderMaterialResource graphMaterialB = MakeGraphMaterialResource(shaderB.sourceHash);

        const SceneMeshPassProgramResolution graphA = passResources.ResolveMeshPassProgram(&graphMaterialA, MeshPassType::BaseOpaque);
        Require(graphA.graphProgram && bgfx::isValid(graphA.program) && !graphA.fellBackToBuiltin,
            "KBMAT-MAT07: A cooked graph material must bind its own GPU graph program");

        const SceneMeshPassProgramResolution graphB = passResources.ResolveMeshPassProgram(&graphMaterialB, MeshPassType::BaseOpaque);
        Require(graphB.graphProgram && bgfx::isValid(graphB.program),
            "KBMAT-MAT07: A second cooked graph material must bind its own GPU graph program");
        Require(graphA.program.idx != graphB.program.idx,
            "KBMAT-MAT07: Two graph materials with different graph source hashes must bind two different programs");

        const SceneMeshPassProgramResolution graphAReuse = passResources.ResolveMeshPassProgram(&graphMaterialA, MeshPassType::BaseOpaque);
        Require(graphAReuse.graphProgram && graphAReuse.program.idx == graphA.program.idx,
            "KBMAT-MAT07: Two materials sharing a graph program key must reuse the same program");

        Require(passResources.ProgramBindStats().graphProgramBindCount == 3U,
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

    SceneCachedDrawCommandKey changedProgram = baseKey;
    changedProgram.materialProgramKey = 0x2222U;

    Require(!(baseKey == changedProgram),
        "KBMAT-MAT07: A graph program key change must produce a different draw command cache key");
    Require(hasher(baseKey) != hasher(changedProgram),
        "KBMAT-MAT07: A graph program key change must invalidate the draw command cache hash");

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
