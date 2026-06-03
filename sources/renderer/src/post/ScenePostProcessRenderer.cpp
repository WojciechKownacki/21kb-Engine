#include "kb/render/post/ScenePostProcessRenderer.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <array>
#include <algorithm>
#include <cstdint>

namespace kb::render {
namespace {

struct PosTexVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
};

[[nodiscard]] bgfx::VertexLayout FullscreenLayout() noexcept {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

void ConfigureFullscreenView(
    bgfx::ViewId viewId,
    bgfx::FrameBufferHandle frameBuffer,
    RenderExtent extent,
    const char* name) {
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

} // namespace

ScenePostProcessRenderer::~ScenePostProcessRenderer() {
    Shutdown();
}

bool ScenePostProcessSubmitDesc::IsValid() const noexcept {
    return bgfx::isValid(sceneColor) && target.IsValid() && viewIds.IsValid();
}

bool SceneTemporalReprojectionDesc::IsValid() const noexcept {
    return bgfx::isValid(depthTexture);
}

bool ScenePostProcessRenderer::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    prefilterProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_bloom_prefilter.sc");
    blurProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_bloom_blur.sc");
    combineProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_bloom_combine.sc");
    motionVectorsProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_motion_vectors.sc");
    taaResolveProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_taa_resolve.sc");
    sourceSampler_ = bgfx::createUniform("s_source", bgfx::UniformType::Sampler);
    bloomSampler_ = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    depthSampler_ = bgfx::createUniform("s_depth", bgfx::UniformType::Sampler);
    historySampler_ = bgfx::createUniform("s_history", bgfx::UniformType::Sampler);
    velocitySampler_ = bgfx::createUniform("s_velocity", bgfx::UniformType::Sampler);
    postParams_ = bgfx::createUniform("u_postParams", bgfx::UniformType::Vec4);
    inverseViewProjectionUniform_ = bgfx::createUniform("u_inverseViewProjection", bgfx::UniformType::Mat4);
    previousViewProjectionUniform_ = bgfx::createUniform("u_previousViewProjection", bgfx::UniformType::Mat4);
    temporalParamsUniform_ = bgfx::createUniform("u_temporalParams", bgfx::UniformType::Vec4);
    fullscreenLayout_ = FullscreenLayout();

    constexpr std::array<PosTexVertex, 3U> triangle{
        PosTexVertex{-1.0F, 1.0F, 0.0F, 0.0F, 0.0F},
        PosTexVertex{3.0F, 1.0F, 0.0F, 2.0F, 0.0F},
        PosTexVertex{-1.0F, -3.0F, 0.0F, 0.0F, 2.0F},
    };
    const bgfx::Memory* memory = bgfx::copy(triangle.data(), static_cast<std::uint32_t>(sizeof(triangle)));
    fullscreenVertexBuffer_ = bgfx::createVertexBuffer(memory, fullscreenLayout_);
    if (bgfx::isValid(fullscreenVertexBuffer_)) {
        bgfx::setName(fullscreenVertexBuffer_, "KB Scene PostProcess Fullscreen Triangle");
    }

    if (!IsInitialized()) {
        Shutdown();
        return false;
    }

    return true;
}

void ScenePostProcessRenderer::Shutdown() noexcept {
    if (bgfx::isValid(fullscreenVertexBuffer_)) {
        bgfx::destroy(fullscreenVertexBuffer_);
        fullscreenVertexBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(postParams_)) {
        bgfx::destroy(postParams_);
        postParams_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(temporalParamsUniform_)) {
        bgfx::destroy(temporalParamsUniform_);
        temporalParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(previousViewProjectionUniform_)) {
        bgfx::destroy(previousViewProjectionUniform_);
        previousViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(inverseViewProjectionUniform_)) {
        bgfx::destroy(inverseViewProjectionUniform_);
        inverseViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(velocitySampler_)) {
        bgfx::destroy(velocitySampler_);
        velocitySampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(historySampler_)) {
        bgfx::destroy(historySampler_);
        historySampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(depthSampler_)) {
        bgfx::destroy(depthSampler_);
        depthSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(bloomSampler_)) {
        bgfx::destroy(bloomSampler_);
        bloomSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(sourceSampler_)) {
        bgfx::destroy(sourceSampler_);
        sourceSampler_ = BGFX_INVALID_HANDLE;
    }
    DestroyPrograms();
}

void ScenePostProcessRenderer::DestroyPrograms() noexcept {
    if (bgfx::isValid(combineProgram_)) {
        bgfx::destroy(combineProgram_);
        combineProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(taaResolveProgram_)) {
        bgfx::destroy(taaResolveProgram_);
        taaResolveProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(motionVectorsProgram_)) {
        bgfx::destroy(motionVectorsProgram_);
        motionVectorsProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(blurProgram_)) {
        bgfx::destroy(blurProgram_);
        blurProgram_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(prefilterProgram_)) {
        bgfx::destroy(prefilterProgram_);
        prefilterProgram_ = BGFX_INVALID_HANDLE;
    }
}

bgfx::TextureHandle ScenePostProcessRenderer::Submit(const ScenePostProcessSubmitDesc& desc) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return BGFX_INVALID_HANDLE;
    }

    const float width = static_cast<float>(std::max(1U, desc.target.extent.width));
    const float height = static_cast<float>(std::max(1U, desc.target.extent.height));
    const bool bloomEnabled = desc.settings.bloomEnabled && desc.settings.bloomStrength > 0.0F;
    const float bloomStrength = std::max(desc.settings.bloomStrength, 0.0F);
    const float bloomThreshold = std::max(desc.settings.bloomThreshold, 0.0F);
    const float bloomSoftKnee = std::clamp(desc.settings.bloomSoftKnee, 0.0F, 1.0F);
    const float bloomRadius = std::max(desc.settings.bloomRadiusPixels, 0.0F);
    const bool taaEnabled = desc.settings.temporalAntiAliasingEnabled && bgfx::isValid(desc.sceneDepth) && desc.temporal.IsValid();
    const float historyBlend = std::clamp(desc.settings.temporalHistoryBlend, 0.0F, 1.0F);
    const float bloomMipCount = static_cast<float>(std::clamp<std::uint8_t>(
        desc.target.bloomMipCount,
        static_cast<std::uint8_t>(1U),
        static_cast<std::uint8_t>(RenderPostProcessTargetBinding::kMaxBloomPyramidMips)));
    bgfx::TextureHandle postSource = desc.sceneColor;

    if (taaEnabled) {
        ConfigureFullscreenView(
            desc.viewIds.postProcessMotionVectors,
            desc.target.motionVectorFrameBuffer,
            desc.target.extent,
            "KB Post Motion Vectors");
        bgfx::setUniform(inverseViewProjectionUniform_, desc.temporal.inverseCurrentViewProjection.data());
        bgfx::setUniform(previousViewProjectionUniform_, desc.temporal.previousViewProjection.data());
        bgfx::setUniform(temporalParamsUniform_, desc.temporal.jitterAndParams.data());
        bgfx::setTexture(0, depthSampler_, desc.sceneDepth);
        SubmitFullscreen(desc.viewIds.postProcessMotionVectors, motionVectorsProgram_, fullscreenVertexBuffer_);
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
        taaEnabled ? "KB Post TAA Resolve" : "KB Post TAA Copy");
    const float temporalParams[4] = {
        historyBlend,
        taaEnabled && desc.temporal.historyValid ? 1.0F : 0.0F,
        1.0F / width,
        1.0F / height,
    };
    bgfx::setUniform(temporalParamsUniform_, temporalParams);
    bgfx::setTexture(0, sourceSampler_, desc.sceneColor);
    bgfx::setTexture(1, historySampler_, desc.target.previousTemporalHistoryTexture);
    bgfx::setTexture(2, velocitySampler_, desc.target.motionVectorTexture);
    SubmitFullscreen(desc.viewIds.postProcessTaaResolve, taaResolveProgram_, fullscreenVertexBuffer_);
    postSource = desc.target.temporalHistoryTexture;

    if (!bloomEnabled) {
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

        ConfigureFullscreenView(
            desc.viewIds.postProcessHdrFinalize,
            desc.target.finalFrameBuffer,
            desc.target.extent,
            "KB Post HDR Finalize");
        const float finalizeParams[4] = {0.0F, 0.0F, 0.0F, 0.0F};
        bgfx::setUniform(postParams_, finalizeParams);
        bgfx::setTexture(0, sourceSampler_, postSource);
        bgfx::setTexture(1, bloomSampler_, desc.target.bloomTexture);
        SubmitFullscreen(desc.viewIds.postProcessHdrFinalize, combineProgram_, fullscreenVertexBuffer_);
        return desc.target.finalTexture;
    }

    ConfigureFullscreenView(
        desc.viewIds.postProcessBloomPrefilter,
        desc.target.bloomMipFrameBuffers[0],
        desc.target.bloomMipExtents[0],
        "KB Post Bloom Prefilter");
    const float prefilterParams[4] = {bloomThreshold, bloomSoftKnee, 0.0F, 0.0F};
    bgfx::setUniform(postParams_, prefilterParams);
    bgfx::setTexture(0, sourceSampler_, postSource);
    SubmitFullscreen(desc.viewIds.postProcessBloomPrefilter, prefilterProgram_, fullscreenVertexBuffer_);

    ConfigureFullscreenView(
        desc.viewIds.postProcessBloomBlurH,
        desc.target.pingMipFrameBuffers[0],
        desc.target.bloomMipExtents[0],
        "KB Post Bloom Blur H");
    const float blurHParams[4] = {(1.0F / width) * bloomRadius, 0.0F, 0.0F, 0.0F};
    bgfx::setUniform(postParams_, blurHParams);
    bgfx::setTexture(0, sourceSampler_, desc.target.bloomTexture);
    SubmitFullscreen(desc.viewIds.postProcessBloomBlurH, blurProgram_, fullscreenVertexBuffer_);

    ConfigureFullscreenView(
        desc.viewIds.postProcessBloomBlurV,
        desc.target.bloomMipFrameBuffers[0],
        desc.target.bloomMipExtents[0],
        "KB Post Bloom Blur V");
    const float blurVParams[4] = {0.0F, (1.0F / height) * bloomRadius, 0.0F, 0.0F};
    bgfx::setUniform(postParams_, blurVParams);
    bgfx::setTexture(0, sourceSampler_, desc.target.pingTexture);
    SubmitFullscreen(desc.viewIds.postProcessBloomBlurV, blurProgram_, fullscreenVertexBuffer_);

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
        const float downsampleParams[4] = {(1.0F / previousMipWidth) * bloomRadius, (1.0F / previousMipHeight) * bloomRadius, sourceLod, 0.0F};
        bgfx::setUniform(postParams_, downsampleParams);
        bgfx::setTexture(0, sourceSampler_, desc.target.bloomTexture);
        SubmitFullscreen(desc.viewIds.postProcessBloomDownsampleViews[viewIndex], blurProgram_, fullscreenVertexBuffer_);

        ConfigureFullscreenView(
            desc.viewIds.postProcessBloomMipBlurHViews[viewIndex],
            desc.target.pingMipFrameBuffers[mip],
            mipExtent,
            "KB Post Bloom Blur H Mip");
        const float mipBlurHParams[4] = {(1.0F / mipWidth) * bloomRadius, 0.0F, targetLod, 0.0F};
        bgfx::setUniform(postParams_, mipBlurHParams);
        bgfx::setTexture(0, sourceSampler_, desc.target.bloomTexture);
        SubmitFullscreen(desc.viewIds.postProcessBloomMipBlurHViews[viewIndex], blurProgram_, fullscreenVertexBuffer_);

        ConfigureFullscreenView(
            desc.viewIds.postProcessBloomMipBlurVViews[viewIndex],
            desc.target.bloomMipFrameBuffers[mip],
            mipExtent,
            "KB Post Bloom Blur V Mip");
        const float mipBlurVParams[4] = {0.0F, (1.0F / mipHeight) * bloomRadius, targetLod, 0.0F};
        bgfx::setUniform(postParams_, mipBlurVParams);
        bgfx::setTexture(0, sourceSampler_, desc.target.pingTexture);
        SubmitFullscreen(desc.viewIds.postProcessBloomMipBlurVViews[viewIndex], blurProgram_, fullscreenVertexBuffer_);
    }

    ConfigureFullscreenView(
        desc.viewIds.postProcessHdrCombine,
        desc.target.combineFrameBuffer,
        desc.target.extent,
        "KB Post HDR Combine");
    const float combineParams[4] = {bloomStrength, bloomMipCount, 0.0F, 0.0F};
    bgfx::setUniform(postParams_, combineParams);
    bgfx::setTexture(0, sourceSampler_, postSource);
    bgfx::setTexture(1, bloomSampler_, desc.target.bloomTexture);
    SubmitFullscreen(desc.viewIds.postProcessHdrCombine, combineProgram_, fullscreenVertexBuffer_);

    ConfigureFullscreenView(
        desc.viewIds.postProcessHdrFinalize,
        desc.target.finalFrameBuffer,
        desc.target.extent,
        "KB Post HDR Finalize");
    const float finalizeParams[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    bgfx::setUniform(postParams_, finalizeParams);
    bgfx::setTexture(0, sourceSampler_, desc.target.combineTexture);
    bgfx::setTexture(1, bloomSampler_, desc.target.bloomTexture);
    SubmitFullscreen(desc.viewIds.postProcessHdrFinalize, combineProgram_, fullscreenVertexBuffer_);

    return desc.target.finalTexture;
}

bool ScenePostProcessRenderer::IsInitialized() const noexcept {
    return bgfx::isValid(prefilterProgram_) && bgfx::isValid(blurProgram_) &&
           bgfx::isValid(combineProgram_) && bgfx::isValid(motionVectorsProgram_) &&
           bgfx::isValid(taaResolveProgram_) && bgfx::isValid(sourceSampler_) &&
           bgfx::isValid(bloomSampler_) && bgfx::isValid(depthSampler_) &&
           bgfx::isValid(historySampler_) && bgfx::isValid(velocitySampler_) &&
           bgfx::isValid(postParams_) && bgfx::isValid(inverseViewProjectionUniform_) &&
           bgfx::isValid(previousViewProjectionUniform_) && bgfx::isValid(temporalParamsUniform_) &&
           bgfx::isValid(fullscreenVertexBuffer_);
}

} // namespace kb::render
