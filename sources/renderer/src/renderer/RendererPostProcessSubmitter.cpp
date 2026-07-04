#include "renderer/RendererPostProcessSubmitter.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/overlay/SceneGridPass.hpp"
#include "kb/render/post/ScenePostProcessRenderer.hpp"
#include "renderer/RendererDebugLog.hpp"
#include "renderer/RendererMatrixMath.hpp"

#include <sstream>

namespace kb::render {
namespace {

[[nodiscard]] const char* BoolText(bool value) noexcept {
    return value ? "true" : "false";
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::TextureHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::FrameBufferHandle handle) noexcept {
    return handle.idx;
}

} // namespace

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
    const std::array<float, 16> currentUnjitteredViewProjection = desc.unjitteredSceneCamera == nullptr
        ? currentViewProjection
        : RendererMatrixMath::ViewProjection(*desc.unjitteredSceneCamera);
    // Reprojection must be jitter-free on both ends: unprojecting with the jittered inverse
    // leaks the current frame's jitter into the motion vectors, so the TAA resolve resamples
    // history at a different sub-pixel offset every frame and static geometry wobbles.
    const std::array<float, 16> inverseCurrentViewProjection = RendererMatrixMath::Inverse(currentUnjitteredViewProjection);
    const std::array<float, 16> previousViewProjection = temporalHistoryValid ? desc.previousViewProjection : currentUnjitteredViewProjection;
    const std::array<float, 2> previousJitter = temporalHistoryValid ? desc.previousJitter : desc.jitter;
    const std::array<float, 4> historyJitterUvParams = {
        previousJitter[0],
        -previousJitter[1],
        desc.jitter[0],
        -desc.jitter[1],
    };
    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();
    const bool effectiveJitter = desc.jitter[0] != 0.0F || desc.jitter[1] != 0.0F;
    const bgfx::TextureHandle sceneDepthTexture = desc.sceneDesc.SceneOverlayDepthTexture();

    {
        std::ostringstream message;
        message << "RendererPostProcessSubmitter submit"
                << " outputFxaa=" << BoolText(desc.postProcessOutput.fxaaEnabled)
                << " outputTaa=" << BoolText(desc.postProcessOutput.temporalAntiAliasingEnabled)
                << " settingsFxaa=" << BoolText(postProcessSettings.fxaaEnabled)
                << " settingsTaa=" << BoolText(postProcessSettings.temporalAntiAliasingEnabled)
                << " settingsJitter=" << BoolText(postProcessSettings.temporalJitterEnabled)
                << " effectiveJitter=" << BoolText(effectiveJitter)
                << " jitterX=" << desc.jitter[0]
                << " jitterY=" << desc.jitter[1]
                << " sceneDepthValid=" << BoolText(bgfx::isValid(sceneDepthTexture))
                << " targetDepth=" << HandleValue(desc.sceneDesc.target.depthTexture)
                << " overlayDepth=" << HandleValue(desc.sceneDesc.editorOverlayDepthTexture)
                << " sampledDepth=" << HandleValue(sceneDepthTexture)
                << " temporalHistoryValid=" << BoolText(temporalHistoryValid)
                << " hasTemporalHistory=" << BoolText(desc.hasTemporalHistory)
                << " frameIndex=" << desc.frameIndex
                << " temporalExtent=" << desc.temporalExtent.width << 'x' << desc.temporalExtent.height
                << " targetExtent=" << desc.sceneDesc.target.viewport.extent.width << 'x' << desc.sceneDesc.target.viewport.extent.height
                << " historyWriteIndex=" << static_cast<unsigned>(postProcessTarget.temporalHistoryWriteIndex)
                << " historyFb=" << HandleValue(postProcessTarget.temporalHistoryFrameBuffer)
                << " historyTex=" << HandleValue(postProcessTarget.temporalHistoryTexture)
                << " previousHistoryTex=" << HandleValue(postProcessTarget.previousTemporalHistoryTexture)
                << " currentVp0=" << currentViewProjection[0]
                << " currentVp5=" << currentViewProjection[5]
                << " currentVp8=" << currentViewProjection[8]
                << " currentVp9=" << currentViewProjection[9]
                << " currentVp10=" << currentViewProjection[10]
                << " currentVp14=" << currentViewProjection[14]
                << " currentUnjitteredVp8=" << currentUnjitteredViewProjection[8]
                << " currentUnjitteredVp9=" << currentUnjitteredViewProjection[9]
                << " previousVp0=" << previousViewProjection[0]
                << " previousVp5=" << previousViewProjection[5]
                << " previousVp8=" << previousViewProjection[8]
                << " previousVp9=" << previousViewProjection[9]
                << " previousVp10=" << previousViewProjection[10]
                << " previousVp14=" << previousViewProjection[14]
                << " previousJitterX=" << previousJitter[0]
                << " previousJitterY=" << previousJitter[1]
                << " historyJitterOffsetX=" << historyJitterUvParams[0]
                << " historyJitterOffsetY=" << historyJitterUvParams[1];
        WriteRendererDebugLog("aa_trace", message.str());
    }

    const bgfx::TextureHandle scenePostProcessOutput = desc.postProcessRenderer.Submit(ScenePostProcessSubmitDesc{
        .sceneColor = desc.sceneDesc.target.colorTexture,
        .sceneDepth = sceneDepthTexture,
        .target = postProcessTarget,
        .viewIds = desc.viewportPlan.viewIds,
        .settings = postProcessSettings,
        .temporal = SceneTemporalReprojectionDesc{
            .depthTexture = sceneDepthTexture,
            .currentViewProjection = currentUnjitteredViewProjection,
            .inverseCurrentViewProjection = inverseCurrentViewProjection,
            .previousViewProjection = previousViewProjection,
            .jitterAndParams = {
                desc.jitter[0],
                desc.jitter[1],
                postProcessSettings.temporalAntiAliasingEnabled ? 1.0F : 0.0F,
                homogeneousDepth ? 1.0F : 0.0F,
            },
            .historyJitterParams = historyJitterUvParams,
            .backgroundPlaneParams = {
                desc.sceneDesc.editorSceneOverlaysEnabled ? 1.0F : 0.0F,
                SceneGridPass::kGridPlaneY,
                0.0F,
                0.0F,
            },
            .historyValid = temporalHistoryValid,
        },
    });
    if (!bgfx::isValid(scenePostProcessOutput)) {
        return BGFX_INVALID_HANDLE;
    }

    desc.temporalExtent = desc.sceneDesc.target.viewport.extent;
    desc.previousViewProjection = currentUnjitteredViewProjection;
    desc.previousJitter = desc.jitter;
    desc.hasTemporalHistory = true;
    {
        std::ostringstream message;
        message << "RendererPostProcessSubmitter temporal state updated"
                << " frameIndex=" << desc.frameIndex
                << " temporalExtent=" << desc.temporalExtent.width << 'x' << desc.temporalExtent.height
                << " hasTemporalHistory=" << BoolText(desc.hasTemporalHistory)
                << " storedVp8=" << desc.previousViewProjection[8]
                << " storedVp9=" << desc.previousViewProjection[9]
                << " storedJitterX=" << desc.previousJitter[0]
                << " storedJitterY=" << desc.previousJitter[1];
        WriteRendererDebugLog("aa_trace", message.str());
    }
    return scenePostProcessOutput;
}

} // namespace kb::render
