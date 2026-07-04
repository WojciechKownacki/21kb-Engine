#include "renderer/RendererFinalCompositeSubmitter.hpp"

#include "renderer/RendererDebugLog.hpp"

#include <sstream>

namespace kb::render {

bool RendererFinalCompositeSubmitter::Submit(
    const FinalCompositePass& finalCompositePass,
    const RenderViewportPlan& viewportPlan,
    const RenderSceneSubmitDesc& desc,
    const PostProcessOutput& postProcessOutput,
    bgfx::TextureHandle scenePostProcessOutput) {
    SceneDisplayOutputTransform outputTransform = postProcessOutput.outputTransform;
    if (!postProcessOutput.tonemapEnabled) {
        outputTransform = SceneDisplayOutputTransform{
            .exposureStops = 0.0F,
            .gamma = 1.0F,
            .tonemap = SceneDisplayTonemapOperator::None,
            .colorGradingLutStrength = 0.0F,
        };
    }

    {
        std::ostringstream message;
        message << "Final composite receive"
                << " postProcessColor=" << scenePostProcessOutput.idx
                << " postProcessTonemap=" << (postProcessOutput.tonemapEnabled ? "true" : "false")
                << " outputTonemap=" << static_cast<int>(outputTransform.tonemap)
                << " exposure=" << outputTransform.exposureStops
                << " gamma=" << outputTransform.gamma
                << " autoExposure=" << (outputTransform.autoExposure.enabled ? "true" : "false")
                << " meteredLum=" << outputTransform.autoExposure.meteredAverageLuminance
                << " colorSpace=" << static_cast<int>(postProcessOutput.colorSpace)
                << " producer=" << PostProcessPassKindName(postProcessOutput.producer)
                << " fxaa=" << (postProcessOutput.fxaaEnabled ? "true" : "false")
                << " taa=" << (postProcessOutput.temporalAntiAliasingEnabled ? "true" : "false")
                << " bloom=" << (postProcessOutput.bloomEnabled ? "true" : "false");
        WriteRendererDebugLog("aa_trace", message.str());
    }

    return finalCompositePass.Submit(FinalCompositePassDesc{
        .viewId = viewportPlan.viewIds.finalComposite,
        .postProcessColor = scenePostProcessOutput,
        .frameBuffer = desc.finalComposite.frameBuffer,
        .extent = desc.finalComposite.extent,
        .outputRect = desc.finalComposite.outputRect,
        .outputTransform = outputTransform,
        .clearRgba = desc.clearRgba,
        .clearTarget = desc.finalComposite.clearTarget,
    });
}

} // namespace kb::render
