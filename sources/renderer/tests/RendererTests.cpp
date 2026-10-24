#include "RendererTestSupport.hpp"
#include "RendererAssertionPolicy.hpp"

#include <bgfx/bgfx.h>

#include <chrono>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <psapi.h>
#endif

namespace kb::render::tests {

std::size_t PeakCommittedBytes() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != FALSE ? counters.PeakPagefileUsage : 0U;
#else
    return 0U;
#endif
}

void RunGraphForwardGpuRenderTests();
void RunGraphProgramLifetimeTests();
void RunRendererScreenUiSubmitTests();
void RunGraphShaderArtifactCacheTests();
void RunGraphShaderNodeCookTests(std::uint32_t shard, std::uint32_t shardCount);
void RunSceneLightGridTests();
void RunSceneLightGridGpuTests();
void RunSkinnedMeshGpuReadbackTests();
void RunPrebuiltBackendShaderGpuTests();
void RunFinalCompositePassTests();
void RunPostProcessChainTests();
void RunRenderFramePipelineTests();
void RunRenderResourceRegistryTests();
void RunAssetImportCatalogCoverageTests();
void RunGltfExternalResourceTests();
void RunRuntimeAssetShaderProviderTests();
void RunRuntimeAssetPackValidationTests();
void RunPackagedMaterialRuntimeTests();
void RunRenderMaterialTypeSchemaTests();
void RunGraphShaderArtifactCookTests();
void RunMaterialProgramRegistryTests();
void RunSceneMeshPassProgramSelectionTests();
void RunRendererRuntimeSubmitTests();
void RunRendererCommandReuseTests();
void RunRendererTransparentGpuReadbackTests();
void RunRendererDefaultSubmissionResultTest();
void RunRendererPostProcessProfileDiagnosticTest();
void RunRendererPostProcessProfileAuthoringTests();
void RunRendererResourceGroupEnsureTests();
void RunRendererSceneSubmitScaleBenchmark(bool spiralLayout);
void RunRendererPacedSceneSubmitStressBenchmark(bool staticMillionSnapshot,
    bool gpuDrivenDispatchEnabled, unsigned maxForwardLights);
void RunRendererVisibilityFeedbackTest();
void RunEditorUIViewTransformValidationTests();
void RunRendererParticleMeshSnapshotSubmitTest();
void RunRendererParticleContactDeathTest();
void RunParticleGpuMotionTests(bool kernelOnly, bgfx::RendererType::Enum backend);
void RunRendererParticleMotionPixelsTest(bgfx::RendererType::Enum backend);
void RunRendererParticleDepthAuthoringTest();
void RunRendererParticleSortModesTest();
void RunRendererScreenSpaceEffectTests();
void RunRendererParticleStripSnapshotSubmitTest();
void RunRendererParticleVolumetricSnapshotSubmitTest();
void RunRendererDetachedViewportFinalCompositePixelsTest();
void RunRendererCapabilityReportTests();
void RunMeshPipelineTests();
void RunSceneDisplayCompositeTests();
void RunSceneExposureMeterTests();
void RunExposureReadbackResetTest();
void RunSceneDepthPolicyTests();
void RunRenderSceneSyncTests();
void RunSceneRenderTargetFormatTests();
void RunSceneRenderExtractorTests();
void RunShaderManifestTests();
void RunShaderPrewarmParseTests();
void RunMeshBakeTests();
void RunWorldHlodMeshBakerTests();
void RunTextureBakeTests();
void RunRuntimeContentStreamingTests();
void RunPackagedWebGpuTextureFallbackTestOnly();
void RunScreenUIDrawBatchTests();
void RunLargeWorldRenderTests();
}

namespace {
void RunSuite(const char* name, void (*run)()) {
    std::fprintf(stderr, "renderer_tests begin %s\n", name);
    std::fflush(stderr);
    const auto started = std::chrono::steady_clock::now();
    run();
    const double milliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    std::fprintf(stderr, "renderer_tests passed %s elapsed_ms=%.3f\n", name, milliseconds);
    std::fflush(stderr);
}
} // namespace

int main(int argc, char** argv) {
    kb::render::tests::InstallRendererAssertionPolicy();
    if (argc == 2 && std::string_view{ argv[1] } == "assert-probe") {
        kb::render::tests::ProbeRendererAssertion();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{argv[1]} == "command-reuse") {
        kb::render::tests::RunMeshPipelineTests();
        kb::render::tests::RunRenderSceneSyncTests();
        kb::render::tests::RunRendererCommandReuseTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{argv[1]} == "mesh-visibility") {
        kb::render::tests::RunMeshPipelineTests();
        kb::render::tests::RunSceneDepthPolicyTests();
        kb::render::tests::RunRenderSceneSyncTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "post-profile-diagnostic") {
        kb::render::tests::RunRendererPostProcessProfileDiagnosticTest();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "post-process-profile") {
        kb::render::tests::RunRendererPostProcessProfileAuthoringTests();
        kb::render::tests::RunSceneExposureMeterTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "exposure-readback") {
        kb::render::tests::RunExposureReadbackResetTest();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "default-submit") {
        kb::render::tests::RunRendererDefaultSubmissionResultTest();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "mesh-bake") {
        kb::render::tests::RunMeshBakeTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "world-hlod") {
        kb::render::tests::RunWorldHlodMeshBakerTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "texture-bake") {
        kb::render::tests::RunTextureBakeTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "content-streaming") {
        kb::render::tests::RunRuntimeContentStreamingTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "webgpu-texture-fallback") {
        kb::render::tests::RunPackagedWebGpuTextureFallbackTestOnly();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "graph-shader-artifact") {
        kb::render::tests::RunGraphShaderArtifactCookTests();
        return EXIT_SUCCESS;
    }
    if (argc >= 2 && std::string_view{ argv[1] } == "graph-shader-node-cook") {
        if (argc != 4) return EXIT_FAILURE;
        const auto parse = [](std::string_view value, std::uint32_t& output) {
            const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
            return result.ec == std::errc{} && result.ptr == value.data() + value.size();
        };
        std::uint32_t shard = 0U;
        std::uint32_t count = 0U;
        if (!parse(argv[2], shard) || !parse(argv[3], count) || count == 0U || count > 64U || shard >= count)
            return EXIT_FAILURE;
        kb::render::tests::RunGraphShaderNodeCookTests(shard, count);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "shader-manifest") {
        kb::render::tests::RunShaderManifestTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "screen-space") {
        kb::render::tests::RunRendererScreenSpaceEffectTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "frame-pipeline") {
        kb::render::tests::RunRenderFramePipelineTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "screen-ui-batch") {
        kb::render::tests::RunScreenUIDrawBatchTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "gltf-external") {
        kb::render::tests::RunGltfExternalResourceTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "import-catalog") {
        kb::render::tests::RunAssetImportCatalogCoverageTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "resource-registry") {
        kb::render::tests::RunRenderResourceRegistryTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "runtime-shader-provider") {
        kb::render::tests::RunRuntimeAssetShaderProviderTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "runtime-pack-validation") {
        kb::render::tests::RunRuntimeAssetPackValidationTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "packaged-material") {
        kb::render::tests::RunPackagedMaterialRuntimeTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "mesh-pipeline") {
        kb::render::tests::RunMeshPipelineTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "transparent-gpu-readback") {
        kb::render::tests::RunRendererTransparentGpuReadbackTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "mesh-pass-program-selection") {
        kb::render::tests::RunSceneMeshPassProgramSelectionTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "graph-forward-gpu") {
        kb::render::tests::RunGraphForwardGpuRenderTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "graph-program-lifetime") {
        kb::render::tests::RunGraphProgramLifetimeTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "runtime-submit") {
        kb::render::tests::RunRendererRuntimeSubmitTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "screen-ui-submit") {
        kb::render::tests::RunRendererScreenUiSubmitTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "backend-shader-gpu") {
        kb::render::tests::RunPrebuiltBackendShaderGpuTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "skinned-gpu-readback") {
        kb::render::tests::RunSkinnedMeshGpuReadbackTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "capabilities") {
        kb::render::tests::RunRendererCapabilityReportTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "exposure-meter") {
        kb::render::tests::RunSceneExposureMeterTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "large-world") {
        kb::render::tests::RunLargeWorldRenderTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "scene-sync") {
        kb::render::tests::RunRenderSceneSyncTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "resource-group-ensure") {
        kb::render::tests::RunRendererResourceGroupEnsureTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "visibility-feedback") {
        kb::render::tests::RunRendererVisibilityFeedbackTest();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "scene-render-target-format") {
        kb::render::tests::RunSceneRenderTargetFormatTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "particle-sort-modes") {
        try {
            kb::render::tests::RunRendererParticleSortModesTest();
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%s\n", error.what());
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && std::string_view{ argv[1] } == "particle-depth-collision") {
        try {
            kb::render::tests::RunRendererParticleDepthAuthoringTest();
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%s\n", error.what());
            return EXIT_FAILURE;
        }
    }
    if (argc >= 2 && (std::string_view{argv[1]} == "particle-motion" || std::string_view{argv[1]} == "particle-motion-kernel")) {
        if ((argc != 2 && argc != 4) || (argc == 4 && std::string_view{argv[2]} != "--backend")) {
            std::fputs("particle-motion expects [--backend d3d11|d3d12|vulkan|opengl]\n", stderr);
            return EXIT_FAILURE;
        }
        auto backend = bgfx::RendererType::Direct3D11;
        if (argc == 4) {
            const std::string_view name{argv[3]};
            if (name == "d3d11") backend = bgfx::RendererType::Direct3D11;
            else if (name == "d3d12") backend = bgfx::RendererType::Direct3D12;
            else if (name == "vulkan") backend = bgfx::RendererType::Vulkan;
            else if (name == "opengl") backend = bgfx::RendererType::OpenGL;
            else {
                std::fputs("particle-motion received an unsupported backend\n", stderr);
                return EXIT_FAILURE;
            }
        }
        kb::render::tests::RunParticleGpuMotionTests(std::string_view{argv[1]} == "particle-motion-kernel", backend);
        if (std::string_view{argv[1]} == "particle-motion") kb::render::tests::RunRendererParticleMotionPixelsTest(backend);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && (std::string_view{ argv[1] } == "particle-contact-death" || std::string_view{ argv[1] } == "particle-collision-fallback")) {
        try {
            kb::render::tests::RunRendererParticleContactDeathTest();
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "%s\n", error.what());
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && std::string_view{ argv[1] } == "particle-mesh-submit") {
        kb::render::tests::RunRendererParticleMeshSnapshotSubmitTest();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "scene-submit-scale") {
        kb::render::tests::RunRendererSceneSubmitScaleBenchmark(false);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "scene-submit-scale-spiral") {
        kb::render::tests::RunRendererSceneSubmitScaleBenchmark(true);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "paced-scene-submit-stress") {
        kb::render::tests::RunRendererPacedSceneSubmitStressBenchmark(false, true, 4U);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "million-scene-snapshot") {
        kb::render::tests::RunRendererPacedSceneSubmitStressBenchmark(true, true, 4U);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "million-scene-snapshot-off4") {
        kb::render::tests::RunRendererPacedSceneSubmitStressBenchmark(true, false, 4U);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "million-scene-snapshot-on0") {
        kb::render::tests::RunRendererPacedSceneSubmitStressBenchmark(true, true, 0U);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "million-scene-snapshot-off0") {
        kb::render::tests::RunRendererPacedSceneSubmitStressBenchmark(true, false, 0U);
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "particle-strip-submit") {
        try {
            kb::render::tests::RunRendererParticleStripSnapshotSubmitTest();
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::fputs(error.what(), stderr);
            std::fputc('\n', stderr);
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && std::string_view{ argv[1] } == "particle-volumetric-submit") {
        try {
            kb::render::tests::RunRendererParticleVolumetricSnapshotSubmitTest();
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::fputs(error.what(), stderr);
            std::fputc('\n', stderr);
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && std::string_view{ argv[1] } == "detached-final-composite") {
        try {
            kb::render::tests::RunRendererDetachedViewportFinalCompositePixelsTest();
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::fputs(error.what(), stderr);
            std::fputc('\n', stderr);
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && std::string_view{argv[1]} == "editor-ui-view") {
        try {
            kb::render::tests::RunEditorUIViewTransformValidationTests();
            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::fputs(error.what(),stderr);
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && std::string_view{argv[1]} == "light-grid") {
        kb::render::tests::RunSceneLightGridTests();
        kb::render::tests::RunSceneLightGridGpuTests();
        return EXIT_SUCCESS;
    }
    const bool integrationOnly = argc == 2 && std::string_view{ argv[1] } == "renderer-integration";
    if (argc != 1 && !integrationOnly) {
        return EXIT_FAILURE;
    }
    RunSuite("RunSceneLightGridTests", &kb::render::tests::RunSceneLightGridTests);
    RunSuite("RunSceneLightGridGpuTests", &kb::render::tests::RunSceneLightGridGpuTests);
    RunSuite("RunGraphForwardGpuRenderTests", &kb::render::tests::RunGraphForwardGpuRenderTests);
    RunSuite("RunFinalCompositePassTests", &kb::render::tests::RunFinalCompositePassTests);
    RunSuite("RunPostProcessChainTests", &kb::render::tests::RunPostProcessChainTests);
    RunSuite("RunRenderFramePipelineTests", &kb::render::tests::RunRenderFramePipelineTests);
    RunSuite("RunRenderResourceRegistryTests", &kb::render::tests::RunRenderResourceRegistryTests);
    RunSuite("RunAssetImportCatalogCoverageTests", &kb::render::tests::RunAssetImportCatalogCoverageTests);
    RunSuite("RunGltfExternalResourceTests", &kb::render::tests::RunGltfExternalResourceTests);
    RunSuite("RunRuntimeAssetShaderProviderTests", &kb::render::tests::RunRuntimeAssetShaderProviderTests);
    RunSuite("RunRuntimeAssetPackValidationTests", &kb::render::tests::RunRuntimeAssetPackValidationTests);
    RunSuite("RunRenderMaterialTypeSchemaTests", &kb::render::tests::RunRenderMaterialTypeSchemaTests);
    RunSuite(integrationOnly ? "RunGraphShaderArtifactCacheTests" : "RunGraphShaderArtifactCookTests",
        integrationOnly ? &kb::render::tests::RunGraphShaderArtifactCacheTests : &kb::render::tests::RunGraphShaderArtifactCookTests);
    RunSuite("RunMaterialProgramRegistryTests", &kb::render::tests::RunMaterialProgramRegistryTests);
    RunSuite("RunSceneMeshPassProgramSelectionTests", &kb::render::tests::RunSceneMeshPassProgramSelectionTests);
    RunSuite("RunRendererCapabilityReportTests", &kb::render::tests::RunRendererCapabilityReportTests);
    RunSuite("RunRendererRuntimeSubmitTests", &kb::render::tests::RunRendererRuntimeSubmitTests);
    RunSuite("RunRendererDefaultSubmissionResultTest", &kb::render::tests::RunRendererDefaultSubmissionResultTest);
    RunSuite("RunMeshPipelineTests", &kb::render::tests::RunMeshPipelineTests);
    RunSuite("RunSceneDisplayCompositeTests", &kb::render::tests::RunSceneDisplayCompositeTests);
    RunSuite("RunSceneExposureMeterTests", &kb::render::tests::RunSceneExposureMeterTests);
    RunSuite("RunSceneDepthPolicyTests", &kb::render::tests::RunSceneDepthPolicyTests);
    RunSuite("RunRenderSceneSyncTests", &kb::render::tests::RunRenderSceneSyncTests);
    RunSuite("RunLargeWorldRenderTests", &kb::render::tests::RunLargeWorldRenderTests);
    RunSuite("RunSceneRenderTargetFormatTests", &kb::render::tests::RunSceneRenderTargetFormatTests);
    RunSuite("RunSceneRenderExtractorTests", &kb::render::tests::RunSceneRenderExtractorTests);
    RunSuite("RunShaderManifestTests", &kb::render::tests::RunShaderManifestTests);
    RunSuite("RunShaderPrewarmParseTests", &kb::render::tests::RunShaderPrewarmParseTests);
    RunSuite("RunScreenUIDrawBatchTests", &kb::render::tests::RunScreenUIDrawBatchTests);
    RunSuite("RunMeshBakeTests", &kb::render::tests::RunMeshBakeTests);
    RunSuite("RunWorldHlodMeshBakerTests", &kb::render::tests::RunWorldHlodMeshBakerTests);
    RunSuite("RunTextureBakeTests", &kb::render::tests::RunTextureBakeTests);
    RunSuite("RunRuntimeContentStreamingTests", &kb::render::tests::RunRuntimeContentStreamingTests);
    return EXIT_SUCCESS;
}
