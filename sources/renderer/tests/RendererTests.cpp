#include <cstdlib>
#include <string_view>

namespace kb::render::tests {
void RunGraphForwardGpuRenderTests();
void RunSkinnedMeshGpuReadbackTests();
void RunFinalCompositePassTests();
void RunPostProcessChainTests();
void RunRenderFramePipelineTests();
void RunRenderResourceRegistryTests();
void RunRenderMaterialTypeSchemaTests();
void RunGraphShaderArtifactCookTests();
void RunMaterialProgramRegistryTests();
void RunSceneMeshPassProgramSelectionTests();
void RunRendererRuntimeSubmitTests();
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
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{ argv[1] } == "resource-registry") {
        kb::render::tests::RunRenderResourceRegistryTests();
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
    if (argc != 1) {
        return EXIT_FAILURE;
    }
    kb::render::tests::RunGraphForwardGpuRenderTests();
    kb::render::tests::RunFinalCompositePassTests();
    kb::render::tests::RunPostProcessChainTests();
    kb::render::tests::RunRenderFramePipelineTests();
    kb::render::tests::RunRenderResourceRegistryTests();
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
    return EXIT_SUCCESS;
}
