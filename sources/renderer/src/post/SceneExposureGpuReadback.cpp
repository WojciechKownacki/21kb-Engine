#include "post/SceneExposureGpuReadback.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "post/SceneExposureHistogramBuilder.hpp"

#include <algorithm>
#include <array>

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

void ConfigureReadbackView(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer) {
    bgfx::setViewName(viewId, "KB HDR Exposure Readback");
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewRect(
        viewId,
        0,
        0,
        ClampToViewExtent(SceneExposureMeter::kGpuHistogramReadbackWidth),
        ClampToViewExtent(SceneExposureMeter::kGpuHistogramReadbackHeight));
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
    bgfx::touch(viewId);
}

} // namespace

bool SceneExposureGpuReadback::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    if (bgfx::getRendererType() == bgfx::RendererType::Noop) {
        return false;
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_TEXTURE_READ_BACK) == 0U ||
        (caps->supported & BGFX_CAPS_TEXTURE_BLIT) == 0U) {
        return false;
    }

    hdrLuminanceProgram_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_post_exposure_luminance.sc");
    hdrSourceSampler_ = bgfx::createUniform("s_source", bgfx::UniformType::Sampler);
    hdrExposureParams_ = bgfx::createUniform("u_exposureParams", bgfx::UniformType::Vec4);
    fullscreenLayout_ = FullscreenLayout();

    constexpr std::array<PosTexVertex, 3U> triangle{
        PosTexVertex{-1.0F, 1.0F, 0.0F, 0.0F, 0.0F},
        PosTexVertex{3.0F, 1.0F, 0.0F, 2.0F, 0.0F},
        PosTexVertex{-1.0F, -3.0F, 0.0F, 0.0F, 2.0F},
    };
    const bgfx::Memory* memory = bgfx::copy(triangle.data(), static_cast<std::uint32_t>(sizeof(triangle)));
    fullscreenVertexBuffer_ = bgfx::createVertexBuffer(memory, fullscreenLayout_);
    if (bgfx::isValid(fullscreenVertexBuffer_)) {
        bgfx::setName(fullscreenVertexBuffer_, "KB HDR Exposure Fullscreen Triangle");
    }

    hdrReadbackBytes_.assign(SceneExposureMeter::kGpuHistogramReadbackByteCount, 0U);
    if (!CreateTarget() || !IsInitialized()) {
        Shutdown();
        return false;
    }

    return true;
}

void SceneExposureGpuReadback::Shutdown() noexcept {
    DestroyResources();
    hdrReadbackBytes_.clear();
    hdrReadbackReadyFrame_ = 0;
    latestHdrAverageLuminance_ = 0.18F;
    hdrReadbackPending_ = false;
    latestHdrSampleValid_ = false;
}

bool SceneExposureGpuReadback::IsInitialized() const noexcept {
    return bgfx::isValid(hdrLuminanceProgram_) && bgfx::isValid(hdrSourceSampler_) &&
           bgfx::isValid(hdrExposureParams_) && bgfx::isValid(fullscreenVertexBuffer_) &&
           bgfx::isValid(hdrReadbackRenderTexture_) && bgfx::isValid(hdrReadbackTexture_) &&
           bgfx::isValid(hdrReadbackFrameBuffer_) &&
           hdrReadbackBytes_.size() == SceneExposureMeter::kGpuHistogramReadbackByteCount;
}

SceneHdrExposureReadbackResult SceneExposureGpuReadback::Submit(const SceneHdrExposureReadbackDesc& desc) noexcept {
    SceneHdrExposureReadbackResult result{
        .hasValidSample = latestHdrSampleValid_,
        .meteredAverageLuminance = latestHdrAverageLuminance_,
    };
    if (!IsInitialized() || !desc.IsValid()) {
        return result;
    }

    result.sampleAvailable = Consume(desc.completedFrame);
    result.hasValidSample = latestHdrSampleValid_;
    result.meteredAverageLuminance = latestHdrAverageLuminance_;
    if (hdrReadbackPending_) {
        return result;
    }

    ConfigureReadbackView(desc.viewId, hdrReadbackFrameBuffer_);
    constexpr float logRange = SceneExposureHistogramBuilder::kHdrReadbackMaxLog2Luminance - SceneExposureHistogramBuilder::kHdrReadbackMinLog2Luminance;
    const float params[4] = {
        static_cast<float>(SceneExposureMeter::kHdrReadbackWidth),
        static_cast<float>(SceneExposureMeter::kHdrReadbackHeight),
        SceneExposureHistogramBuilder::kHdrReadbackMinLog2Luminance,
        1.0F / logRange,
    };
    bgfx::setUniform(hdrExposureParams_, params);
    bgfx::setTexture(0, hdrSourceSampler_, desc.hdrColor);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, fullscreenVertexBuffer_);
    bgfx::submit(desc.viewId, hdrLuminanceProgram_);
    bgfx::blit(desc.viewId, hdrReadbackTexture_, 0U, 0U, hdrReadbackRenderTexture_);
    std::ranges::fill(hdrReadbackBytes_, 0U);
    hdrReadbackReadyFrame_ = bgfx::readTexture(hdrReadbackTexture_, hdrReadbackBytes_.data());
    hdrReadbackPending_ = true;
    result.submitted = true;
    return result;
}

void SceneExposureGpuReadback::Reset() noexcept {
    latestHdrAverageLuminance_ = 0.18F;
    latestHdrSampleValid_ = false;
    hdrReadbackReadyFrame_ = 0;
    hdrReadbackPending_ = false;
    std::ranges::fill(hdrReadbackBytes_, 0U);
}

void SceneExposureGpuReadback::DestroyResources() noexcept {
    if (bgfx::isValid(hdrReadbackFrameBuffer_)) {
        bgfx::destroy(hdrReadbackFrameBuffer_);
        hdrReadbackFrameBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(hdrReadbackTexture_)) {
        bgfx::destroy(hdrReadbackTexture_);
        hdrReadbackTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(hdrReadbackRenderTexture_)) {
        bgfx::destroy(hdrReadbackRenderTexture_);
        hdrReadbackRenderTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fullscreenVertexBuffer_)) {
        bgfx::destroy(fullscreenVertexBuffer_);
        fullscreenVertexBuffer_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(hdrExposureParams_)) {
        bgfx::destroy(hdrExposureParams_);
        hdrExposureParams_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(hdrSourceSampler_)) {
        bgfx::destroy(hdrSourceSampler_);
        hdrSourceSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(hdrLuminanceProgram_)) {
        bgfx::destroy(hdrLuminanceProgram_);
        hdrLuminanceProgram_ = BGFX_INVALID_HANDLE;
    }
}

bool SceneExposureGpuReadback::Consume(std::uint32_t completedFrame) noexcept {
    if (!hdrReadbackPending_ || completedFrame < hdrReadbackReadyFrame_) {
        return false;
    }

    hdrReadbackPending_ = false;
    latestHdrAverageLuminance_ = SceneExposureHistogramBuilder::MeterAverageLuminance(
        SceneExposureHistogramBuilder::BuildGpuHistogramReadbackHistogram(hdrReadbackBytes_));
    latestHdrSampleValid_ = true;
    return true;
}

bool SceneExposureGpuReadback::CreateTarget() noexcept {
    constexpr std::uint64_t renderFlags =
        BGFX_TEXTURE_RT |
        BGFX_SAMPLER_U_CLAMP |
        BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT |
        BGFX_SAMPLER_MAG_POINT |
        BGFX_SAMPLER_MIP_POINT;
    hdrReadbackRenderTexture_ = bgfx::createTexture2D(
        static_cast<std::uint16_t>(SceneExposureMeter::kGpuHistogramReadbackWidth),
        static_cast<std::uint16_t>(SceneExposureMeter::kGpuHistogramReadbackHeight),
        false,
        1U,
        bgfx::TextureFormat::RGBA8,
        renderFlags);
    if (!bgfx::isValid(hdrReadbackRenderTexture_)) {
        return false;
    }
    bgfx::setName(hdrReadbackRenderTexture_, "KB HDR Exposure Readback RT RGBA8");

    constexpr std::uint64_t readbackFlags =
        BGFX_TEXTURE_BLIT_DST |
        BGFX_TEXTURE_READ_BACK |
        BGFX_SAMPLER_U_CLAMP |
        BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT |
        BGFX_SAMPLER_MAG_POINT |
        BGFX_SAMPLER_MIP_POINT;
    hdrReadbackTexture_ = bgfx::createTexture2D(
        static_cast<std::uint16_t>(SceneExposureMeter::kGpuHistogramReadbackWidth),
        static_cast<std::uint16_t>(SceneExposureMeter::kGpuHistogramReadbackHeight),
        false,
        1U,
        bgfx::TextureFormat::RGBA8,
        readbackFlags);
    if (!bgfx::isValid(hdrReadbackTexture_)) {
        return false;
    }
    bgfx::setName(hdrReadbackTexture_, "KB HDR Exposure Readback RGBA8");

    hdrReadbackFrameBuffer_ = bgfx::createFrameBuffer(1U, &hdrReadbackRenderTexture_, false);
    if (!bgfx::isValid(hdrReadbackFrameBuffer_)) {
        return false;
    }
    bgfx::setName(hdrReadbackFrameBuffer_, "KB HDR Exposure Readback FrameBuffer");
    return true;
}

} // namespace kb::render
