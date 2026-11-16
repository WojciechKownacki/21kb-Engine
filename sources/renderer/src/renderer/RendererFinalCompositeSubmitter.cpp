#include "renderer/RendererFinalCompositeSubmitter.hpp"

#include "renderer/RendererDebugLog.hpp"

#include <algorithm>
#include <sstream>

namespace kb::render {

SceneDisplayOutputTransform RendererFinalCompositeSubmitter::DisplayTransform(const PostProcessOutput& postProcessOutput) noexcept {
    SceneDisplayOutputTransform outputTransform = postProcessOutput.outputTransform;
    if (!postProcessOutput.tonemapEnabled) {
        // Exposure and the display encoding still apply, or linear scene light would reach the
        // screen as display values.
        outputTransform.tonemap = SceneDisplayTonemapOperator::None;
    }
    return outputTransform;
}

float RendererFinalCompositeSubmitter::VignetteIntensity(const PostProcessOutput& postProcessOutput) noexcept {
    const ScenePostProcessSettings& settings = postProcessOutput.postProcessSettings;
    return settings.vignetteEnabled ? std::clamp(settings.vignetteIntensity, 0.0F, 1.0F) : 0.0F;
}

FullscreenTextureImageEffects RendererFinalCompositeSubmitter::ImageEffects(
    const PostProcessOutput& postProcessOutput, std::uint64_t frameIndex) noexcept {
    const ScenePostProcessSettings& settings = postProcessOutput.postProcessSettings;
    FullscreenTextureImageEffects effects;
    if (settings.chromaticAberrationEnabled) {
        effects.chromaticAberrationIntensity = std::clamp(settings.chromaticAberrationIntensity, 0.0F, 1.0F);
    }
    if (settings.filmGrainEnabled) {
        effects.filmGrainIntensity = std::clamp(settings.filmGrainIntensity, 0.0F, 1.0F);
        effects.filmGrainSize = std::clamp(settings.filmGrainSize,
            ScenePostProcessSettings::kFilmGrainSizeMin, ScenePostProcessSettings::kFilmGrainSizeMax);
        effects.grainFrame = frameIndex;
    }
    return effects;
}

bool RendererFinalCompositeSubmitter::Submit(
    const FinalCompositePass& finalCompositePass,
    const RenderViewportPlan& viewportPlan,
    const RenderSceneSubmitDesc& desc,
    const PostProcessOutput& postProcessOutput,
    bgfx::TextureHandle scenePostProcessOutput,
    std::uint64_t frameIndex) {
    SceneDisplayOutputTransform outputTransform = DisplayTransform(postProcessOutput);
    outputTransform.hdr = desc.finalComposite.hdr;
    if (outputTransform.hdr.stage != FullscreenTextureHdrStage::Off) {
        // The HDR10 encode reads the image back as sRGB display values.
        outputTransform.gamma = SceneDisplayOutputTransform{}.gamma;
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
        .vignetteIntensity = VignetteIntensity(postProcessOutput),
        // FXAA is the post-tonemap anti-aliasing: it runs when selected and TAA (which resolves before the
        // tonemap) is not.
        .antiAliasing = postProcessOutput.fxaaEnabled && !postProcessOutput.temporalAntiAliasingEnabled,
        .postProcessExtent = postProcessOutput.extent,
        .upscale = desc.upscaledExtent.IsValid(),
        .imageEffects = ImageEffects(postProcessOutput, frameIndex),
    });
}

} // namespace kb::render
