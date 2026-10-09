#include "RendererTestSupport.hpp"

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
void RunRendererResourceGroupEnsureTests();
void RunRendererSceneSubmitScaleBenchmark(bool spiralLayout);
void RunRendererPacedSceneSubmitStressBenchmark(bool staticMillionSnapshot,
    bool gpuDrivenDispatchEnabled, unsigned maxForwardLights);
void RunRendererVisibilityFeedbackTest();
void RunEditorUIViewTransformValidationTests();
void RunRendererParticleMeshSnapshotSubmitTest();
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
}

int main(int argc, char** argv) {
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
    if (argc == 2 && std::string_view{ argv[1] } == "shader-manifest") {
        kb::render::tests::RunShaderManifestTests();
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
    if (argc != 1) {
        return EXIT_FAILURE;
    }
    kb::render::tests::RunSceneLightGridTests();
    kb::render::tests::RunSceneLightGridGpuTests();
    kb::render::tests::RunGraphForwardGpuRenderTests();
    kb::render::tests::RunFinalCompositePassTests();
    kb::render::tests::RunPostProcessChainTests();
    kb::render::tests::RunRenderFramePipelineTests();
    kb::render::tests::RunRenderResourceRegistryTests();
    kb::render::tests::RunAssetImportCatalogCoverageTests();
    kb::render::tests::RunGltfExternalResourceTests();
    kb::render::tests::RunRuntimeAssetShaderProviderTests();
    kb::render::tests::RunRuntimeAssetPackValidationTests();
    kb::render::tests::RunRenderMaterialTypeSchemaTests();
    kb::render::tests::RunGraphShaderArtifactCookTests();
    kb::render::tests::RunMaterialProgramRegistryTests();
    kb::render::tests::RunSceneMeshPassProgramSelectionTests();
    kb::render::tests::RunRendererCapabilityReportTests();
    kb::render::tests::RunRendererRuntimeSubmitTests();
    kb::render::tests::RunRendererDefaultSubmissionResultTest();
    kb::render::tests::RunMeshPipelineTests();
    kb::render::tests::RunSceneDisplayCompositeTests();
    kb::render::tests::RunSceneExposureMeterTests();
    kb::render::tests::RunSceneDepthPolicyTests();
    kb::render::tests::RunRenderSceneSyncTests();
    kb::render::tests::RunSceneRenderTargetFormatTests();
    kb::render::tests::RunSceneRenderExtractorTests();
    kb::render::tests::RunShaderManifestTests();
    kb::render::tests::RunShaderPrewarmParseTests();
    kb::render::tests::RunScreenUIDrawBatchTests();
    kb::render::tests::RunMeshBakeTests();
    kb::render::tests::RunWorldHlodMeshBakerTests();
    kb::render::tests::RunTextureBakeTests();
    kb::render::tests::RunRuntimeContentStreamingTests();
    return EXIT_SUCCESS;
}
