#include "renderer/RendererPostProcessSubmitter.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/post/ScenePostProcessRenderer.hpp"
#include "renderer/RendererMatrixMath.hpp"

namespace kb::render {

bgfx::TextureHandle RendererPostProcessSubmitter::Submit(const RendererPostProcessSubmitDesc& desc) {
    ScenePostProcessSettings postProcessSettings = desc.postProcessOutput.postProcessSettings;
    postProcessSettings.bloomEnabled = desc.postProcessOutput.bloomEnabled;
    postProcessSettings.fxaaEnabled = desc.postProcessOutput.fxaaEnabled;
    postProcessSettings.temporalAntiAliasingEnabled = desc.postProcessOutput.temporalAntiAliasingEnabled;
    postProcessSettings.temporalJitterEnabled = desc.postProcessOutput.temporalAntiAliasingEnabled;

    RenderPostProcessTargetBinding postProcessTarget = desc.sceneDesc.postProcess;
    postProcessTarget.SelectTemporalHistory(desc.frameIndex);

    const bool temporalHistoryValid = desc.hasTemporalHistory && desc.temporalExtent == desc.sceneDesc.target.viewport.extent;
    const std::array<float, 16> currentViewProjection = desc.sceneCamera == nullptr ? RendererMatrixMath::Identity() : RendererMatrixMath::ViewProjection(*desc.sceneCamera);
    const std::array<float, 16> inverseCurrentViewProjection = RendererMatrixMath::Inverse(currentViewProjection);
    const std::array<float, 16> previousViewProjection = temporalHistoryValid ? desc.previousViewProjection : currentViewProjection;
    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();

    const bgfx::TextureHandle scenePostProcessOutput = desc.postProcessRenderer.Submit(ScenePostProcessSubmitDesc{
        .sceneColor = desc.sceneDesc.target.colorTexture,
        .sceneDepth = desc.sceneDesc.target.depthTexture,
        .target = postProcessTarget,
        .viewIds = desc.viewportPlan.viewIds,
        .settings = postProcessSettings,
        .temporal = SceneTemporalReprojectionDesc{
            .depthTexture = desc.sceneDesc.target.depthTexture,
            .currentViewProjection = currentViewProjection,
            .inverseCurrentViewProjection = inverseCurrentViewProjection,
            .previousViewProjection = previousViewProjection,
            .jitterAndParams = {
                desc.jitter[0],
                desc.jitter[1],
                postProcessSettings.temporalAntiAliasingEnabled ? 1.0F : 0.0F,
                homogeneousDepth ? 1.0F : 0.0F,
            },
            .historyValid = temporalHistoryValid,
        },
    });
    if (!bgfx::isValid(scenePostProcessOutput)) {
        return BGFX_INVALID_HANDLE;
    }

    desc.temporalExtent = desc.sceneDesc.target.viewport.extent;
    desc.previousViewProjection = currentViewProjection;
    desc.hasTemporalHistory = true;
    return scenePostProcessOutput;
}

} // namespace kb::render
