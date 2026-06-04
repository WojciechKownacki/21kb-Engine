#include "renderer/RendererExposureSubmitter.hpp"

namespace kb::render {

SceneRenderExposureSubmitStats RendererExposureSubmitter::Submit(
    SceneExposureMeter& exposureMeter,
    PostProcessOutput& postProcessOutput,
    const RenderSceneSubmitDesc& desc,
    const RenderViewportPlan& viewportPlan,
    const RenderScene& renderScene,
    const SceneRenderLightingConfig& lightingConfig,
    std::uint32_t lastCompletedFrame) {
    SceneRenderExposureSubmitStats exposureStats{
        .viewportId = desc.target.viewport.id.value,
        .viewportIndex = desc.target.viewport.viewportIndex,
        .meteredAverageLuminance = postProcessOutput.outputTransform.autoExposure.meteredAverageLuminance,
        .adaptedAverageLuminance = postProcessOutput.outputTransform.autoExposure.meteredAverageLuminance,
        .autoExposureEnabled = postProcessOutput.outputTransform.autoExposure.enabled,
        .temporalAdaptationEnabled = postProcessOutput.outputTransform.autoExposure.temporalAdaptationEnabled,
    };
    if (!postProcessOutput.outputTransform.autoExposure.enabled) {
        return exposureStats;
    }

    const FullscreenTextureAutoExposureSettings& autoExposure = postProcessOutput.outputTransform.autoExposure;
    float meteredAverageLuminance = autoExposure.meteredAverageLuminance;
    bool shouldApplyTemporalAdaptation = true;
    if (postProcessOutput.postProcessSettings.autoExposureMetering == ScenePostProcessSettings::AutoExposureMeteringMode::Manual) {
        exposureStats.source = SceneRenderExposureMeteringSource::Manual;
        shouldApplyTemporalAdaptation = false;
    } else if (postProcessOutput.postProcessSettings.autoExposureMetering == ScenePostProcessSettings::AutoExposureMeteringMode::HdrColor) {
        const SceneHdrExposureReadbackResult hdrReadback = exposureMeter.SubmitHdrReadback(SceneHdrExposureReadbackDesc{
            .viewId = viewportPlan.viewIds.postProcessExposureReadback,
            .hdrColor = desc.target.colorTexture,
            .extent = desc.target.viewport.extent,
            .completedFrame = lastCompletedFrame,
        });
        exposureStats.gpuReadbackSubmitted = hdrReadback.submitted;
        exposureStats.gpuReadbackSampleAvailable = hdrReadback.sampleAvailable;
        exposureStats.gpuReadbackSampleValid = hdrReadback.hasValidSample;
        if (hdrReadback.hasValidSample) {
            meteredAverageLuminance = hdrReadback.meteredAverageLuminance;
            exposureStats.source = SceneRenderExposureMeteringSource::HdrReadback;
        } else {
            meteredAverageLuminance = SceneExposureMeter::EstimateAverageLuminance(renderScene, lightingConfig);
            exposureStats.source = SceneRenderExposureMeteringSource::HdrReadbackPendingFallback;
        }
    } else {
        meteredAverageLuminance = SceneExposureMeter::EstimateAverageLuminance(renderScene, lightingConfig);
        exposureStats.source = SceneRenderExposureMeteringSource::SceneLighting;
    }

    exposureStats.meteredAverageLuminance = meteredAverageLuminance;
    exposureStats.adaptedAverageLuminance = shouldApplyTemporalAdaptation
        ? exposureMeter.Update(meteredAverageLuminance, SceneExposureAdaptationDesc{
              .enabled = autoExposure.temporalAdaptationEnabled,
              .deltaSeconds = 1.0F / 60.0F,
              .brightAdaptationRate = autoExposure.brightAdaptationRate,
              .darkAdaptationRate = autoExposure.darkAdaptationRate,
          })
        : meteredAverageLuminance;
    postProcessOutput.outputTransform.autoExposure.meteredAverageLuminance = exposureStats.adaptedAverageLuminance;
    return exposureStats;
}

} // namespace kb::render
