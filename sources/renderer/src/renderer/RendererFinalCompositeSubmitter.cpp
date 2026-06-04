#include "renderer/RendererFinalCompositeSubmitter.hpp"

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
