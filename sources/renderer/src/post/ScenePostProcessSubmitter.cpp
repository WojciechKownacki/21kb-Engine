#include "kb/render/post/ScenePostProcessRenderer.hpp"

#include "renderer/RendererDebugLog.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <sstream>

namespace kb::render {
namespace {

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

void ConfigureFullscreenView(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer, RenderExtent extent, const char* name) {
    bgfx::setViewName(viewId, name);
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewRect(viewId, 0, 0, ClampToViewExtent(extent.width), ClampToViewExtent(extent.height));
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    bgfx::touch(viewId);
}

void SubmitFullscreen(bgfx::ViewId viewId, bgfx::ProgramHandle program, bgfx::VertexBufferHandle vertexBuffer) {
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, vertexBuffer);
    bgfx::submit(viewId, program);
}

struct PostProcessSubmitSettings {
    float width = 1.0F;
    float height = 1.0F;
    bool bloomEnabled = false;
    bool taaEnabled = false;
    bool motionBlurEnabled = false;
    float bloomStrength = 0.0F;
    float bloomThreshold = 0.0F;
    float bloomSoftKnee = 0.0F;
    float bloomRadius = 0.0F;
    float historyBlend = 0.0F;
};

// Share of each bloom mip in the bloom, from mip 0; the shares add up to one.
constexpr std::array<float, RenderPostProcessTargetBinding::kMaxBloomPyramidMips> kBloomMipWeights{0.42F, 0.26F, 0.16F, 0.09F, 0.05F, 0.02F};

[[nodiscard]] const char* BoolText(bool value) noexcept {
    return value ? "true" : "false";
}

[[nodiscard]] PostProcessSubmitSettings ResolveSettings(const ScenePostProcessSubmitDesc& desc) noexcept {
    const auto finite = [](float value, float maximum) {
        return std::isnan(value) ? 0.0F : std::clamp(value, 0.0F, maximum);
    };
    return PostProcessSubmitSettings{
        .width = static_cast<float>(std::max(1U, desc.target.extent.width)),
        .height = static_cast<float>(std::max(1U, desc.target.extent.height)),
        .bloomEnabled = desc.settings.bloomEnabled && desc.settings.bloomStrength > 0.0F,
        .taaEnabled = desc.settings.temporalAntiAliasingEnabled && bgfx::isValid(desc.sceneDepth) && desc.temporal.IsValid(),
        .motionBlurEnabled = desc.settings.motionBlurEnabled && desc.settings.motionBlurIntensity > 0.0F &&
            bgfx::isValid(desc.sceneDepth) && desc.temporal.IsValid(),
        .bloomStrength = finite(desc.settings.bloomStrength, 65504.0F),
        .bloomThreshold = finite(desc.settings.bloomThreshold, 65504.0F),
        .bloomSoftKnee = finite(desc.settings.bloomSoftKnee, 1.0F),
        .bloomRadius = finite(desc.settings.bloomRadiusPixels, 65504.0F),
        .historyBlend = std::clamp(desc.settings.temporalHistoryBlend, 0.0F, 1.0F),
    };
}

} // namespace

class ScenePostProcessRenderer::Submitter {
public:
    explicit Submitter(const ScenePostProcessRenderer& renderer) noexcept
        : renderer_(renderer) {}

    [[nodiscard]] bgfx::TextureHandle Submit(const ScenePostProcessSubmitDesc& desc) const {
        const PostProcessSubmitSettings settings = ResolveSettings(desc);
        {
            std::ostringstream message;
            message << "ScenePostProcessRenderer receive"
                    << " requestedFxaa=" << BoolText(desc.settings.fxaaEnabled)
                    << " requestedTaa=" << BoolText(desc.settings.temporalAntiAliasingEnabled)
                    << " requestedJitter=" << BoolText(desc.settings.temporalJitterEnabled)
                    << " resolvedTaa=" << BoolText(settings.taaEnabled)
                    << " historyBlend=" << settings.historyBlend
                    << " sceneDepthValid=" << BoolText(bgfx::isValid(desc.sceneDepth))
                    << " temporalValid=" << BoolText(desc.temporal.IsValid())
                    << " historyValid=" << BoolText(desc.temporal.historyValid)
                    << " extent=" << static_cast<unsigned>(desc.target.extent.width) << 'x' << static_cast<unsigned>(desc.target.extent.height);
            WriteRendererDebugLog("aa_trace", message.str());
        }
        const bgfx::TextureHandle postSource = SubmitAntiAliasing(desc, settings);
        {
            std::ostringstream message;
            message << "ScenePostProcessRenderer AA output"
                    << " path=" << (settings.taaEnabled ? "TAA" : "Copy")
                    << " postSource=" << postSource.idx
                    << " bloom=" << BoolText(settings.bloomEnabled);
            WriteRendererDebugLog("aa_trace", message.str());
        }
        // The anti-aliased image is the output when nothing combines into it: the material chain and the display
        // composite read it in place, as they read the combine target otherwise, so no pass copies it.
        if (!settings.bloomEnabled && !desc.settings.depthOfFieldEnabled && !settings.motionBlurEnabled) {
            return postSource;
        }

        if (settings.bloomEnabled) SubmitBloomPyramid(desc, settings, postSource);
        SubmitBloomCombine(desc, settings, postSource);
        return desc.target.combineTexture;
    }

private:
    [[nodiscard]] bgfx::TextureHandle SubmitAntiAliasing(const ScenePostProcessSubmitDesc& desc, const PostProcessSubmitSettings& settings) const {
        // Motion vectors feed the TAA resolve and the motion blur of the HDR combine pass.
        if (settings.taaEnabled || settings.motionBlurEnabled) {
            ConfigureFullscreenView(
                desc.viewIds.postProcessMotionVectors,
                desc.target.motionVectorFrameBuffer,
                desc.target.extent,
                "KB Post Motion Vectors");
            bgfx::setUniform(renderer_.inverseViewProjectionUniform_, desc.temporal.inverseCurrentViewProjection.data());
            bgfx::setUniform(renderer_.previousViewProjectionUniform_, desc.temporal.previousViewProjection.data());
            bgfx::setUniform(renderer_.temporalParamsUniform_, desc.temporal.jitterAndParams.data());
            bgfx::setUniform(renderer_.postParams_, desc.temporal.backgroundPlaneParams.data());
            bgfx::setTexture(0, renderer_.depthSampler_, desc.sceneDepth);
            SubmitFullscreen(desc.viewIds.postProcessMotionVectors, renderer_.motionVectorsProgram_, renderer_.fullscreenVertexBuffer_);
        } else {
            ConfigureFullscreenView(
                desc.viewIds.postProcessMotionVectors,
                desc.target.motionVectorFrameBuffer,
                desc.target.extent,
                "KB Post Motion Vectors Disabled");
        }

        ConfigureFullscreenView(
            desc.viewIds.postProcessTaaResolve,
            desc.target.temporalHistoryFrameBuffer,
            desc.target.extent,
            settings.taaEnabled ? "KB Post TAA Resolve" : "KB Post AA Copy");
        {
            std::ostringstream message;
            message << "ScenePostProcessRenderer SubmitAntiAliasing"
                    << " view=" << desc.viewIds.postProcessTaaResolve
                    << " path=" << (settings.taaEnabled ? "TAA" : "Copy")
                    << " taa=" << BoolText(settings.taaEnabled)
                    << " motionVectors=" << BoolText(settings.taaEnabled)
                    << " historyBlend=" << settings.historyBlend
                    << " historyValid=" << BoolText(desc.temporal.historyValid)
                    << " historyJitterX=" << desc.temporal.historyJitterParams[0]
                    << " historyJitterY=" << desc.temporal.historyJitterParams[1]
                    << " currentJitterX=" << desc.temporal.historyJitterParams[2]
                    << " currentJitterY=" << desc.temporal.historyJitterParams[3]
                    << " targetHistoryTexture=" << desc.target.temporalHistoryTexture.idx
                    << " previousHistoryTexture=" << desc.target.previousTemporalHistoryTexture.idx;
            WriteRendererDebugLog("aa_trace", message.str());
        }
        // FXAA is not resolved here: it filters the display image after the tonemap (fs_present_tex.sc).
        const float temporalParams[4] = {
            settings.historyBlend,
            settings.taaEnabled && desc.temporal.historyValid ? 1.0F : 0.0F,
            1.0F / settings.width,
            1.0F / settings.height,
        };
        bgfx::setUniform(renderer_.temporalParamsUniform_, temporalParams);
        bgfx::setTexture(0, renderer_.sourceSampler_, desc.sceneColor);
        bgfx::setTexture(1, renderer_.historySampler_, desc.target.previousTemporalHistoryTexture);
        bgfx::setTexture(2, renderer_.velocitySampler_, desc.target.motionVectorTexture);
        SubmitFullscreen(desc.viewIds.postProcessTaaResolve, renderer_.taaResolveProgram_, renderer_.fullscreenVertexBuffer_);
        return desc.target.temporalHistoryTexture;
    }

    // The horizontal blur of mip k lands in the ping pyramid for even k and in the scratch pyramid for odd k, so
    // an upsample pass reads mip k and the sum of the smaller mips from one texture while writing the other.
    [[nodiscard]] static bgfx::TextureHandle BloomBlurTexture(const ScenePostProcessSubmitDesc& desc, std::uint8_t mip) noexcept {
        return mip % 2U == 0U ? desc.target.pingTexture : desc.target.bloomScratchTexture;
    }

    [[nodiscard]] static bgfx::FrameBufferHandle BloomBlurFrameBuffer(
        const ScenePostProcessSubmitDesc& desc, std::uint8_t pyramidMip, std::uint8_t level) noexcept {
        return pyramidMip % 2U == 0U ? desc.target.pingMipFrameBuffers[level] : desc.target.bloomScratchMipFrameBuffers[level];
    }

    void SubmitBloomPyramid(const ScenePostProcessSubmitDesc& desc, const PostProcessSubmitSettings& settings, bgfx::TextureHandle postSource) const {
        ConfigureFullscreenView(
            desc.viewIds.postProcessBloomPrefilter,
            desc.target.bloomMipFrameBuffers[0],
            desc.target.bloomMipExtents[0],
            "KB Post Bloom Prefilter");
        const float prefilterParams[4] = {settings.bloomThreshold, settings.bloomSoftKnee, 0.0F, 0.0F};
        bgfx::setUniform(renderer_.postParams_, prefilterParams);
        bgfx::setTexture(0, renderer_.sourceSampler_, postSource);
        SubmitFullscreen(desc.viewIds.postProcessBloomPrefilter, renderer_.prefilterProgram_, renderer_.fullscreenVertexBuffer_);

        SubmitBloomDownsampleChain(desc, settings);
        SubmitBloomUpsampleChain(desc, settings);
    }

    // Mip 0 and every smaller mip are blurred horizontally; each smaller mip is a 13-tap downsample of the
    // previous mip's horizontal blur, written to the bloom pyramid.
    void SubmitBloomDownsampleChain(const ScenePostProcessSubmitDesc& desc, const PostProcessSubmitSettings& settings) const {
        for (std::uint8_t mip = 0U; mip < desc.target.bloomMipCount; ++mip) {
            const RenderExtent mipExtent = desc.target.bloomMipExtents[mip];
            const float mipWidth = static_cast<float>(std::max(1U, mipExtent.width));
            const float targetLod = static_cast<float>(mip);
            if (mip > 0U) {
                const std::size_t viewIndex = static_cast<std::size_t>(mip - 1U);
                const RenderExtent previousMipExtent = desc.target.bloomMipExtents[mip - 1U];
                const float previousMipWidth = static_cast<float>(std::max(1U, previousMipExtent.width));
                const float previousMipHeight = static_cast<float>(std::max(1U, previousMipExtent.height));
                ConfigureFullscreenView(
                    desc.viewIds.postProcessBloomDownsampleViews[viewIndex],
                    desc.target.bloomMipFrameBuffers[mip],
                    mipExtent,
                    "KB Post Bloom Downsample Mip");
                const float downsampleParams[4] = {(1.0F / previousMipWidth) * settings.bloomRadius,
                    (1.0F / previousMipHeight) * settings.bloomRadius, static_cast<float>(mip - 1U), 0.0F};
                bgfx::setUniform(renderer_.postParams_, downsampleParams);
                bgfx::setTexture(0, renderer_.sourceSampler_, BloomBlurTexture(desc, static_cast<std::uint8_t>(mip - 1U)));
                SubmitFullscreen(desc.viewIds.postProcessBloomDownsampleViews[viewIndex], renderer_.blurProgram_, renderer_.fullscreenVertexBuffer_);
            }

            const std::uint16_t blurHView = mip == 0U
                ? desc.viewIds.postProcessBloomBlurH
                : desc.viewIds.postProcessBloomMipBlurHViews[static_cast<std::size_t>(mip - 1U)];
            ConfigureFullscreenView(blurHView, BloomBlurFrameBuffer(desc, mip, mip), mipExtent,
                mip == 0U ? "KB Post Bloom Blur H" : "KB Post Bloom Blur H Mip");
            const float blurHParams[4] = {(1.0F / mipWidth) * settings.bloomRadius, 0.0F, targetLod, 0.0F};
            bgfx::setUniform(renderer_.postParams_, blurHParams);
            bgfx::setTexture(0, renderer_.sourceSampler_, desc.target.bloomTexture);
            SubmitFullscreen(blurHView, renderer_.blurProgram_, renderer_.fullscreenVertexBuffer_);
        }
    }

    // From the smallest mip back to mip 0, each pass blurs its mip vertically and adds the tent-filtered sum of
    // the smaller mips, scaled so every mip keeps its share. Mip 0 ends in the bloom texture, which the combine
    // reads with a single sample.
    void SubmitBloomUpsampleChain(const ScenePostProcessSubmitDesc& desc, const PostProcessSubmitSettings& settings) const {
        for (std::uint8_t mip = desc.target.bloomMipCount; mip-- > 0U;) {
            const RenderExtent mipExtent = desc.target.bloomMipExtents[mip];
            const float mipHeight = static_cast<float>(std::max(1U, mipExtent.height));
            const bool hasSmallerMips = mip + 1U < desc.target.bloomMipCount;
            const std::uint16_t view = mip == 0U
                ? desc.viewIds.postProcessBloomBlurV
                : desc.viewIds.postProcessBloomMipBlurVViews[static_cast<std::size_t>(mip - 1U)];
            const bgfx::FrameBufferHandle target = mip == 0U
                ? desc.target.bloomMipFrameBuffers[0]
                : BloomBlurFrameBuffer(desc, static_cast<std::uint8_t>(mip - 1U), mip);
            ConfigureFullscreenView(view, target, mipExtent, mip == 0U ? "KB Post Bloom Blur V" : "KB Post Bloom Upsample Mip");
            const float upsampleParams[4] = {0.0F, (1.0F / mipHeight) * settings.bloomRadius, static_cast<float>(mip),
                hasSmallerMips ? kBloomMipWeights[mip + 1U] / kBloomMipWeights[mip] : 0.0F};
            bgfx::setUniform(renderer_.postParams_, upsampleParams);
            bgfx::setTexture(0, renderer_.sourceSampler_, BloomBlurTexture(desc, mip));
            SubmitFullscreen(view, renderer_.blurProgram_, renderer_.fullscreenVertexBuffer_);
        }
    }

    void SubmitBloomCombine(const ScenePostProcessSubmitDesc& desc, const PostProcessSubmitSettings& settings, bgfx::TextureHandle postSource) const {
        ConfigureFullscreenView(
            desc.viewIds.postProcessHdrCombine,
            desc.target.combineFrameBuffer,
            desc.target.extent,
            "KB Post HDR Combine");
        // The upsample chain leaves the bloom sum divided by the share of mip 0.
        const float combineParams[4] = {settings.bloomEnabled ? settings.bloomStrength * kBloomMipWeights[0] : 0.0F, 0.0F, 0.0F, 0.0F};
        bgfx::setUniform(renderer_.postParams_, combineParams);
        const float dofParams[4] = {desc.settings.depthOfFieldFocusDistance, desc.settings.depthOfFieldFocusRange,
            desc.settings.depthOfFieldEnabled ? desc.settings.depthOfFieldMaxRadiusPixels : 0.0F,
            desc.temporal.jitterAndParams[3]};
        bgfx::setUniform(renderer_.dofParamsUniform_, dofParams);
        bgfx::setUniform(renderer_.dofViewDepthPlaneUniform_, desc.temporal.viewDepthPlane.data());
        bgfx::setUniform(renderer_.inverseViewProjectionUniform_, desc.temporal.inverseCurrentViewProjection.data());
        // The longest blur is a fraction of the image width, so it covers the same share of the screen at any size.
        const float motionBlurParams[4] = {
            settings.motionBlurEnabled ? std::clamp(desc.settings.motionBlurIntensity, 0.0F, 1.0F) : 0.0F,
            static_cast<float>(std::clamp(desc.settings.motionBlurSamples, ScenePostProcessSettings::kMotionBlurSamplesMin,
                ScenePostProcessSettings::kMotionBlurSamplesMax)),
            std::max(4.0F, settings.width * 0.04F),
            0.0F,
        };
        bgfx::setUniform(renderer_.motionBlurParamsUniform_, motionBlurParams);
        bgfx::setTexture(0, renderer_.sourceSampler_, postSource);
        bgfx::setTexture(1, renderer_.bloomSampler_, desc.target.bloomTexture);
        bgfx::setTexture(2, renderer_.depthSampler_, bgfx::isValid(desc.sceneDepth) ? desc.sceneDepth : postSource);
        bgfx::setTexture(3, renderer_.velocitySampler_, settings.motionBlurEnabled ? desc.target.motionVectorTexture : postSource);
        SubmitFullscreen(desc.viewIds.postProcessHdrCombine, renderer_.combineProgram_, renderer_.fullscreenVertexBuffer_);
    }

    const ScenePostProcessRenderer& renderer_;
};

bgfx::TextureHandle ScenePostProcessRenderer::Submit(const ScenePostProcessSubmitDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return BGFX_INVALID_HANDLE;
    }

    return Submitter{*this}.Submit(desc);
}

} // namespace kb::render
