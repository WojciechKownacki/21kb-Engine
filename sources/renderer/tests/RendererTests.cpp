#include <cstdlib>

namespace kb::render::tests {
void RunFinalCompositePassTests();
void RunPostProcessChainTests();
void RunRenderFramePipelineTests();
void RunRenderResourceRegistryTests();
void RunRendererRuntimeSubmitTests();
void RunMeshPipelineTests();
void RunSceneDisplayCompositeTests();
void RunSceneDepthPolicyTests();
void RunRenderSceneSyncTests();
void RunSceneRenderTargetFormatTests();
void RunSceneRenderExtractorTests();
}

int main() {
    kb::render::tests::RunFinalCompositePassTests();
    kb::render::tests::RunPostProcessChainTests();
    kb::render::tests::RunRenderFramePipelineTests();
    kb::render::tests::RunRenderResourceRegistryTests();
    kb::render::tests::RunRendererRuntimeSubmitTests();
    kb::render::tests::RunMeshPipelineTests();
    kb::render::tests::RunSceneDisplayCompositeTests();
    kb::render::tests::RunSceneDepthPolicyTests();
    kb::render::tests::RunRenderSceneSyncTests();
    kb::render::tests::RunSceneRenderTargetFormatTests();
    kb::render::tests::RunSceneRenderExtractorTests();
    return EXIT_SUCCESS;
}
