#include <cstdlib>

namespace kb::render::tests {
void RunGraphForwardGpuRenderTests();
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
}

int main() {
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
    return EXIT_SUCCESS;
}
