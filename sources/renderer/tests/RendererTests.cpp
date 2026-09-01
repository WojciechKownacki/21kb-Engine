#include <cstdlib>
#include <cstdio>
#include <exception>
#include <string_view>

namespace kb::render::tests {
void RunGraphForwardGpuRenderTests();
void RunSkinnedMeshGpuReadbackTests();
void RunFinalCompositePassTests();
void RunPostProcessChainTests();
void RunRenderFramePipelineTests();
void RunRenderResourceRegistryTests();
void RunRuntimeAssetShaderProviderTests();
void RunRuntimeAssetPackValidationTests();
void RunPackagedMaterialRuntimeTests();
void RunRenderMaterialTypeSchemaTests();
void RunGraphShaderArtifactCookTests();
void RunMaterialProgramRegistryTests();
void RunSceneMeshPassProgramSelectionTests();
void RunRendererRuntimeSubmitTests();
void RunRendererParticleMeshSnapshotSubmitTest();
void RunRendererParticleStripSnapshotSubmitTest();
void RunRendererParticleVolumetricSnapshotSubmitTest();
void RunRendererDetachedViewportFinalCompositePixelsTest();
void RunRendererCapabilityReportTests();
void RunMeshPipelineTests();
void RunSceneDisplayCompositeTests();
void RunSceneExposureMeterTests();
void RunSceneDepthPolicyTests();
void RunRenderSceneSyncTests();
void RunSceneRenderTargetFormatTests();
void RunSceneRenderExtractorTests();
void RunShaderManifestTests();
void RunShaderPrewarmParseTests();
void RunMeshBakeTests();
void RunTextureBakeTests();
void RunPackagedWebGpuTextureFallbackTestOnly();
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{ argv[1] } == "mesh-bake") {
        kb::render::tests::RunMeshBakeTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "texture-bake") {
        kb::render::tests::RunTextureBakeTests();
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
    if (argc == 2 && std::string_view{ argv[1] } == "mesh-pass-program-selection") {
        kb::render::tests::RunSceneMeshPassProgramSelectionTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "graph-forward-gpu") {
        kb::render::tests::RunGraphForwardGpuRenderTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "skinned-gpu-readback") {
        kb::render::tests::RunSkinnedMeshGpuReadbackTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "scene-sync") {
        kb::render::tests::RunRenderSceneSyncTests();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && std::string_view{ argv[1] } == "particle-mesh-submit") {
        kb::render::tests::RunRendererParticleMeshSnapshotSubmitTest();
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
    if (argc != 1) {
        return EXIT_FAILURE;
    }
    kb::render::tests::RunGraphForwardGpuRenderTests();
    kb::render::tests::RunFinalCompositePassTests();
    kb::render::tests::RunPostProcessChainTests();
    kb::render::tests::RunRenderFramePipelineTests();
    kb::render::tests::RunRenderResourceRegistryTests();
    kb::render::tests::RunRuntimeAssetShaderProviderTests();
    kb::render::tests::RunRuntimeAssetPackValidationTests();
    kb::render::tests::RunRenderMaterialTypeSchemaTests();
    kb::render::tests::RunGraphShaderArtifactCookTests();
    kb::render::tests::RunMaterialProgramRegistryTests();
    kb::render::tests::RunSceneMeshPassProgramSelectionTests();
    kb::render::tests::RunRendererCapabilityReportTests();
    kb::render::tests::RunRendererRuntimeSubmitTests();
    kb::render::tests::RunMeshPipelineTests();
    kb::render::tests::RunSceneDisplayCompositeTests();
    kb::render::tests::RunSceneExposureMeterTests();
    kb::render::tests::RunSceneDepthPolicyTests();
    kb::render::tests::RunRenderSceneSyncTests();
    kb::render::tests::RunSceneRenderTargetFormatTests();
    kb::render::tests::RunSceneRenderExtractorTests();
    kb::render::tests::RunShaderManifestTests();
    kb::render::tests::RunShaderPrewarmParseTests();
    kb::render::tests::RunMeshBakeTests();
    kb::render::tests::RunTextureBakeTests();
    return EXIT_SUCCESS;
}
