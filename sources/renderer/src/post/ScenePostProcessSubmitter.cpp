#include "kb/render/post/ScenePostProcessRenderer.hpp"

#include <algorithm>
#include <cstdint>

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
    bool fxaaEnabled = false;
    float bloomStrength = 0.0F;
    float bloomThreshold = 0.0F;
    float bloomSoftKnee = 0.0F;
    float bloomRadius = 0.0F;
    float historyBlend = 0.0F;
    float bloomMipCount = 1.0F;
};

[[nodiscard]] PostProcessSubmitSettings ResolveSettings(const ScenePostProcessSubmitDesc& desc) noexcept {
    return PostProcessSubmitSettings{
        .width = static_cast<float>(std::max(1U, desc.target.extent.width)),
        .height = static_cast<float>(std::max(1U, desc.target.extent.height)),
        .bloomEnabled = desc.settings.bloomEnabled && desc.settings.bloomStrength > 0.0F,
        .taaEnabled = desc.settings.temporalAntiAliasingEnabled && bgfx::isValid(desc.sceneDepth) && desc.temporal.IsValid(),
        .fxaaEnabled = !desc.settings.temporalAntiAliasingEnabled && desc.settings.fxaaEnabled,
        .bloomStrength = std::max(desc.settings.bloomStrength, 0.0F),
        .bloomThreshold = std::max(desc.settings.bloomThreshold, 0.0F),
        .bloomSoftKnee = std::clamp(desc.settings.bloomSoftKnee, 0.0F, 1.0F),
        .bloomRadius = std::max(desc.settings.bloomRadiusPixels, 0.0F),
        .historyBlend = std::clamp(desc.settings.temporalHistoryBlend, 0.0F, 1.0F),
        .bloomMipCount = static_cast<float>(std::clamp<std::uint8_t>(
            desc.target.bloomMipCount,
            static_cast<std::uint8_t>(1U),
            static_cast<std::uint8_t>(RenderPostProcessTargetBinding::kMaxBloomPyramidMips))),
    };
}

} // namespace

class ScenePostProcessRenderer::Submitter {
public:
    explicit Submitter(const ScenePostProcessRenderer& renderer) noexcept
        : renderer_(renderer) {}

    [[nodiscard]] bgfx::TextureHandle Submit(const ScenePostProcessSubmitDesc& desc) const {
        const PostProcessSubmitSettings settings = ResolveSettings(desc);
        const bgfx::TextureHandle postSource = SubmitAntiAliasing(desc, settings);
        if (!settings.bloomEnabled) {
            return SubmitWithoutBloom(desc, postSource);
        }

        SubmitBloomPyramid(desc, settings, postSource);
        SubmitBloomCombine(desc, settings, postSource);
        return SubmitFinalize(desc, desc.target.combineTexture);
    }

private:
    [[nodiscard]] bgfx::TextureHandle SubmitAntiAliasing(const ScenePostProcessSubmitDesc& desc, const PostProcessSubmitSettings& settings) const {
        if (settings.taaEnabled) {
            ConfigureFullscreenView(
                desc.viewIds.postProcessMotionVectors,
                desc.target.motionVectorFrameBuffer,
                desc.target.extent,
                "KB Post Motion Vectors");
            bgfx::setUniform(renderer_.inverseViewProjectionUniform_, desc.temporal.inverseCurrentViewProjection.data());
            bgfx::setUniform(renderer_.previousViewProjectionUniform_, desc.temporal.previousViewProjection.data());
            bgfx::setUniform(renderer_.temporalParamsUniform_, desc.temporal.jitterAndParams.data());
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
            settings.taaEnabled ? "KB Post TAA Resolve" : (settings.fxaaEnabled ? "KB Post FXAA" : "KB Post AA Copy"));
        if (settings.fxaaEnabled) {
            const float fxaaParams[4] = {1.0F / settings.width, 1.0F / settings.height, 0.0F, 0.0F};
            bgfx::setUniform(renderer_.temporalParamsUniform_, fxaaParams);
            bgfx::setTexture(0, renderer_.sourceSampler_, desc.sceneColor);
            SubmitFullscreen(desc.viewIds.postProcessTaaResolve, renderer_.fxaaProgram_, renderer_.fullscreenVertexBuffer_);
            return desc.target.temporalHistoryTexture;
        }
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

    [[nodiscard]] bgfx::TextureHandle SubmitWithoutBloom(const ScenePostProcessSubmitDesc& desc, bgfx::TextureHandle postSource) const {
        ConfigureFullscreenView(
            desc.viewIds.postProcessBloomPrefilter,
            desc.target.bloomFrameBuffer,
            desc.target.extent,
            "KB Post Bloom Prefilter Disabled");
        ConfigureFullscreenView(
            desc.viewIds.postProcessBloomBlurH,
            desc.target.pingFrameBuffer,
            desc.target.extent,
            "KB Post Bloom Blur H Disabled");
        ConfigureFullscreenView(
            desc.viewIds.postProcessBloomBlurV,
            desc.target.bloomFrameBuffer,
            desc.target.extent,
            "KB Post Bloom Blur V Disabled");
        ConfigureFullscreenView(
            desc.viewIds.postProcessHdrCombine,
            desc.target.combineFrameBuffer,
            desc.target.extent,
            "KB Post HDR Combine Disabled");
        return SubmitFinalize(desc, postSource);
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

        SubmitBloomMipZeroBlur(desc, settings);
        SubmitBloomHigherMips(desc, settings);
    }

    void SubmitBloomMipZeroBlur(const ScenePostProcessSubmitDesc& desc, const PostProcessSubmitSettings& settings) const {
        ConfigureFullscreenView(
            desc.viewIds.postProcessBloomBlurH,
            desc.target.pingMipFrameBuffers[0],
            desc.target.bloomMipExtents[0],
            "KB Post Bloom Blur H");
        const float blurHParams[4] = {(1.0F / settings.width) * settings.bloomRadius, 0.0F, 0.0F, 0.0F};
        bgfx::setUniform(renderer_.postParams_, blurHParams);
        bgfx::setTexture(0, renderer_.sourceSampler_, desc.target.bloomTexture);
        SubmitFullscreen(desc.viewIds.postProcessBloomBlurH, renderer_.blurProgram_, renderer_.fullscreenVertexBuffer_);

        ConfigureFullscreenView(
            desc.viewIds.postProcessBloomBlurV,
            desc.target.bloomMipFrameBuffers[0],
            desc.target.bloomMipExtents[0],
            "KB Post Bloom Blur V");
        const float blurVParams[4] = {0.0F, (1.0F / settings.height) * settings.bloomRadius, 0.0F, 0.0F};
        bgfx::setUniform(renderer_.postParams_, blurVParams);
        bgfx::setTexture(0, renderer_.sourceSampler_, desc.target.pingTexture);
        SubmitFullscreen(desc.viewIds.postProcessBloomBlurV, renderer_.blurProgram_, renderer_.fullscreenVertexBuffer_);
    }

    void SubmitBloomHigherMips(const ScenePostProcessSubmitDesc& desc, const PostProcessSubmitSettings& settings) const {
        for (std::uint8_t mip = 1U; mip < desc.target.bloomMipCount; ++mip) {
            const std::size_t viewIndex = static_cast<std::size_t>(mip - 1U);
            const RenderExtent mipExtent = desc.target.bloomMipExtents[mip];
            const RenderExtent previousMipExtent = desc.target.bloomMipExtents[mip - 1U];
            const float mipWidth = static_cast<float>(std::max(1U, mipExtent.width));
            const float mipHeight = static_cast<float>(std::max(1U, mipExtent.height));
            const float previousMipWidth = static_cast<float>(std::max(1U, previousMipExtent.width));
            const float previousMipHeight = static_cast<float>(std::max(1U, previousMipExtent.height));
            const float sourceLod = static_cast<float>(mip - 1U);
            const float targetLod = static_cast<float>(mip);

            ConfigureFullscreenView(
                desc.viewIds.postProcessBloomDownsampleViews[viewIndex],
                desc.target.bloomMipFrameBuffers[mip],
                mipExtent,
                "KB Post Bloom Downsample Mip");
            const float downsampleParams[4] = {(1.0F / previousMipWidth) * settings.bloomRadius, (1.0F / previousMipHeight) * settings.bloomRadius, sourceLod, 0.0F};
            bgfx::setUniform(renderer_.postParams_, downsampleParams);
            bgfx::setTexture(0, renderer_.sourceSampler_, desc.target.bloomTexture);
            SubmitFullscreen(desc.viewIds.postProcessBloomDownsampleViews[viewIndex], renderer_.blurProgram_, renderer_.fullscreenVertexBuffer_);

            ConfigureFullscreenView(
                desc.viewIds.postProcessBloomMipBlurHViews[viewIndex],
                desc.target.pingMipFrameBuffers[mip],
                mipExtent,
                "KB Post Bloom Blur H Mip");
            const float mipBlurHParams[4] = {(1.0F / mipWidth) * settings.bloomRadius, 0.0F, targetLod, 0.0F};
            bgfx::setUniform(renderer_.postParams_, mipBlurHParams);
            bgfx::setTexture(0, renderer_.sourceSampler_, desc.target.bloomTexture);
            SubmitFullscreen(desc.viewIds.postProcessBloomMipBlurHViews[viewIndex], renderer_.blurProgram_, renderer_.fullscreenVertexBuffer_);

            ConfigureFullscreenView(
                desc.viewIds.postProcessBloomMipBlurVViews[viewIndex],
                desc.target.bloomMipFrameBuffers[mip],
                mipExtent,
                "KB Post Bloom Blur V Mip");
            const float mipBlurVParams[4] = {0.0F, (1.0F / mipHeight) * settings.bloomRadius, targetLod, 0.0F};
            bgfx::setUniform(renderer_.postParams_, mipBlurVParams);
            bgfx::setTexture(0, renderer_.sourceSampler_, desc.target.pingTexture);
            SubmitFullscreen(desc.viewIds.postProcessBloomMipBlurVViews[viewIndex], renderer_.blurProgram_, renderer_.fullscreenVertexBuffer_);
        }
    }

    void SubmitBloomCombine(const ScenePostProcessSubmitDesc& desc, const PostProcessSubmitSettings& settings, bgfx::TextureHandle postSource) const {
        ConfigureFullscreenView(
            desc.viewIds.postProcessHdrCombine,
            desc.target.combineFrameBuffer,
            desc.target.extent,
            "KB Post HDR Combine");
        const float combineParams[4] = {settings.bloomStrength, settings.bloomMipCount, 0.0F, 0.0F};
        bgfx::setUniform(renderer_.postParams_, combineParams);
        bgfx::setTexture(0, renderer_.sourceSampler_, postSource);
        bgfx::setTexture(1, renderer_.bloomSampler_, desc.target.bloomTexture);
        SubmitFullscreen(desc.viewIds.postProcessHdrCombine, renderer_.combineProgram_, renderer_.fullscreenVertexBuffer_);
    }

    [[nodiscard]] bgfx::TextureHandle SubmitFinalize(const ScenePostProcessSubmitDesc& desc, bgfx::TextureHandle sourceTexture) const {
        ConfigureFullscreenView(
            desc.viewIds.postProcessHdrFinalize,
            desc.target.finalFrameBuffer,
            desc.target.extent,
            "KB Post HDR Finalize");
        const float finalizeParams[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        bgfx::setUniform(renderer_.postParams_, finalizeParams);
        bgfx::setTexture(0, renderer_.sourceSampler_, sourceTexture);
        bgfx::setTexture(1, renderer_.bloomSampler_, desc.target.bloomTexture);
        SubmitFullscreen(desc.viewIds.postProcessHdrFinalize, renderer_.combineProgram_, renderer_.fullscreenVertexBuffer_);
        return desc.target.finalTexture;
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
