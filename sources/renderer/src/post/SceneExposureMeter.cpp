#include "kb/render/post/SceneExposureMeter.hpp"

#include "kb/render/ShaderLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace kb::render {
namespace {

constexpr float kMinAverageLuminance = 0.0001F;
constexpr float kMaxAverageLuminance = 100000.0F;
constexpr float kHdrReadbackMinLog2Luminance = -12.0F;
constexpr float kHdrReadbackMaxLog2Luminance = 16.0F;

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

[[nodiscard]] float LinearLuminance(float r, float g, float b) noexcept {
    return std::max(0.0F, (0.2126F * r) + (0.7152F * g) + (0.0722F * b));
}

[[nodiscard]] float EnvironmentLuminance(const SceneRenderLightingConfig& config) noexcept {
    float luminance = LinearLuminance(config.ambientColor[0], config.ambientColor[1], config.ambientColor[2]) *
        std::max(config.ambientIntensity, 0.0F);

    switch (config.environmentMode) {
    case SceneRenderEnvironmentMode::Disabled:
        break;
    case SceneRenderEnvironmentMode::Constant:
        luminance += LinearLuminance(config.environmentZenithColor[0], config.environmentZenithColor[1], config.environmentZenithColor[2]) *
            std::max(config.environmentDiffuseIntensity, 0.0F) * 0.35F;
        break;
    case SceneRenderEnvironmentMode::Hemisphere: {
        const float zenith = LinearLuminance(config.environmentZenithColor[0], config.environmentZenithColor[1], config.environmentZenithColor[2]);
        const float ground = LinearLuminance(config.environmentGroundColor[0], config.environmentGroundColor[1], config.environmentGroundColor[2]);
        luminance += ((zenith + ground) * 0.5F) * std::max(config.environmentDiffuseIntensity, 0.0F) * 0.35F;
        break;
    }
    case SceneRenderEnvironmentMode::ImageBased:
        luminance += std::max(config.ibl.diffuseIntensity, 0.0F) * std::max(config.environmentDiffuseIntensity, 0.0F) * 0.35F;
        luminance += std::max(config.ibl.specularIntensity, 0.0F) * std::max(config.environmentSpecularIntensity, 0.0F) * 0.15F;
        break;
    }

    return luminance;
}

[[nodiscard]] float LightContribution(const LightRenderProxyDesc& light) noexcept {
    if (!light.visible || light.intensity <= 0.0F) {
        return 0.0F;
    }

    const float colorLuminance = LinearLuminance(light.color[0], light.color[1], light.color[2]);
    switch (light.kind) {
    case RenderLightKind::Directional:
        return colorLuminance * light.intensity * 0.45F;
    case RenderLightKind::Point:
        return colorLuminance * light.intensity * 0.08F;
    case RenderLightKind::Spot:
        return colorLuminance * light.intensity * 0.12F;
    case RenderLightKind::AreaRect:
    case RenderLightKind::AreaDisk:
    case RenderLightKind::Tube:
        return colorLuminance * light.intensity * 0.18F;
    }

    return 0.0F;
}

void AddHistogramSample(SceneExposureHistogram& histogram, float luminance, float weight) noexcept {
    if (luminance <= 0.0F || weight <= 0.0F) {
        return;
    }

    const float logLuminance = std::log2(std::clamp(luminance, kMinAverageLuminance, kMaxAverageLuminance));
    const float range = std::max(histogram.maxLog2Luminance - histogram.minLog2Luminance, 0.001F);
    const float normalized = std::clamp((logLuminance - histogram.minLog2Luminance) / range, 0.0F, 0.999999F);
    const auto bin = static_cast<std::uint32_t>(normalized * static_cast<float>(SceneExposureHistogram::kBinCount));
    histogram.bins[std::min(bin, SceneExposureHistogram::kBinCount - 1U)] += weight;
    histogram.totalWeight += weight;
}

void AddEncodedHdrReadbackSample(SceneExposureHistogram& histogram, std::uint8_t normalizedLogLuminance, std::uint8_t weight) noexcept {
    if (weight == 0U) {
        return;
    }

    const float normalized = (static_cast<float>(normalizedLogLuminance) + 0.5F) / 255.0F;
    const std::uint32_t bin = std::min(
        static_cast<std::uint32_t>(normalized * static_cast<float>(SceneExposureHistogram::kBinCount)),
        SceneExposureHistogram::kBinCount - 1U);
    const float sampleWeight = static_cast<float>(weight) / 255.0F;
    histogram.bins[bin] += sampleWeight;
    histogram.totalWeight += sampleWeight;
}

void AddEncodedGpuHistogramBin(SceneExposureHistogram& histogram, std::uint32_t bin, std::uint8_t weight) noexcept {
    if (weight == 0U || bin >= SceneExposureHistogram::kBinCount) {
        return;
    }

    const float sampleWeight = static_cast<float>(weight) / 255.0F;
    histogram.bins[bin] += sampleWeight;
    histogram.totalWeight += sampleWeight;
}

[[nodiscard]] float BinCenterLuminance(const SceneExposureHistogram& histogram, std::uint32_t bin) noexcept {
    const float range = histogram.maxLog2Luminance - histogram.minLog2Luminance;
    const float binCenter = (static_cast<float>(bin) + 0.5F) / static_cast<float>(SceneExposureHistogram::kBinCount);
    return std::exp2(histogram.minLog2Luminance + (binCenter * range));
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

bool SceneHdrExposureReadbackDesc::IsValid() const noexcept {
    return bgfx::isValid(hdrColor) && extent.IsValid();
}

float SceneExposureMeter::EstimateAverageLuminance(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig) noexcept {
    return MeterAverageLuminance(BuildLightingHistogram(scene, lightingConfig));
}

SceneExposureHistogram SceneExposureMeter::BuildLightingHistogram(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig) noexcept {
    SceneExposureHistogram histogram{};
    AddHistogramSample(histogram, EnvironmentLuminance(lightingConfig), 1.0F);
    for (const auto& [entityId, light] : scene.LightProxies()) {
        static_cast<void>(entityId);
        const float contribution = LightContribution(light.desc);
        AddHistogramSample(histogram, contribution, light.desc.kind == RenderLightKind::Directional ? 1.0F : 0.35F);
    }

    if (histogram.totalWeight <= 0.0F) {
        AddHistogramSample(histogram, kMinAverageLuminance, 1.0F);
    }
    return histogram;
}

SceneExposureHistogram SceneExposureMeter::BuildHdrReadbackHistogram(std::span<const std::uint8_t> rgba8Pixels) noexcept {
    SceneExposureHistogram histogram{
        .minLog2Luminance = kHdrReadbackMinLog2Luminance,
        .maxLog2Luminance = kHdrReadbackMaxLog2Luminance,
    };
    const std::size_t pixelCount = rgba8Pixels.size() / kHdrReadbackBytesPerPixel;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const std::size_t offset = pixel * kHdrReadbackBytesPerPixel;
        AddEncodedHdrReadbackSample(histogram, rgba8Pixels[offset], rgba8Pixels[offset + 3U]);
    }

    if (histogram.totalWeight <= 0.0F) {
        AddHistogramSample(histogram, kMinAverageLuminance, 1.0F);
    }
    return histogram;
}

SceneExposureHistogram SceneExposureMeter::BuildGpuHistogramReadbackHistogram(std::span<const std::uint8_t> rgba8Pixels) noexcept {
    SceneExposureHistogram histogram{
        .minLog2Luminance = kHdrReadbackMinLog2Luminance,
        .maxLog2Luminance = kHdrReadbackMaxLog2Luminance,
    };
    const std::size_t binCount = std::min<std::size_t>(rgba8Pixels.size() / kHdrReadbackBytesPerPixel, SceneExposureHistogram::kBinCount);
    for (std::size_t bin = 0; bin < binCount; ++bin) {
        const std::size_t offset = bin * kHdrReadbackBytesPerPixel;
        AddEncodedGpuHistogramBin(histogram, static_cast<std::uint32_t>(bin), rgba8Pixels[offset + 3U]);
    }

    if (histogram.totalWeight <= 0.0F) {
        AddHistogramSample(histogram, kMinAverageLuminance, 1.0F);
    }
    return histogram;
}

float SceneExposureMeter::MeterAverageLuminance(const SceneExposureHistogram& histogram, SceneExposureMeteringDesc desc) noexcept {
    if (histogram.totalWeight <= 0.0F) {
        return kMinAverageLuminance;
    }

    const float low = std::clamp(desc.lowPercentile, 0.0F, 1.0F);
    const float high = std::clamp(desc.highPercentile, low, 1.0F);
    const float lowCutoff = histogram.totalWeight * low;
    const float highCutoff = histogram.totalWeight * high;
    float cumulative = 0.0F;
    float weightedLogSum = 0.0F;
    float acceptedWeight = 0.0F;

    for (std::uint32_t bin = 0; bin < SceneExposureHistogram::kBinCount; ++bin) {
        const float binWeight = histogram.bins[bin];
        if (binWeight <= 0.0F) {
            continue;
        }

        const float binStart = cumulative;
        const float binEnd = cumulative + binWeight;
        cumulative = binEnd;
        const float accepted = std::max(0.0F, std::min(binEnd, highCutoff) - std::max(binStart, lowCutoff));
        if (accepted <= 0.0F) {
            continue;
        }

        weightedLogSum += std::log2(BinCenterLuminance(histogram, bin)) * accepted;
        acceptedWeight += accepted;
    }

    if (acceptedWeight <= 0.0F) {
        return kMinAverageLuminance;
    }
    return std::clamp(std::exp2(weightedLogSum / acceptedWeight), kMinAverageLuminance, kMaxAverageLuminance);
}

bool SceneExposureMeter::InitializeGpuResources() {
    if (IsGpuInitialized()) {
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

    hdrReadbackBytes_.assign(kGpuHistogramReadbackByteCount, 0U);
    if (!CreateHdrReadbackTarget() || !IsGpuInitialized()) {
        ShutdownGpuResources();
        return false;
    }

    return true;
}

void SceneExposureMeter::ShutdownGpuResources() noexcept {
    DestroyGpuResources();
    hdrReadbackBytes_.clear();
    hdrReadbackReadyFrame_ = 0;
    latestHdrAverageLuminance_ = 0.18F;
    hdrReadbackPending_ = false;
    latestHdrSampleValid_ = false;
}

bool SceneExposureMeter::IsGpuInitialized() const noexcept {
    return bgfx::isValid(hdrLuminanceProgram_) && bgfx::isValid(hdrSourceSampler_) &&
           bgfx::isValid(hdrExposureParams_) && bgfx::isValid(fullscreenVertexBuffer_) &&
           bgfx::isValid(hdrReadbackRenderTexture_) && bgfx::isValid(hdrReadbackTexture_) &&
           bgfx::isValid(hdrReadbackFrameBuffer_) &&
           hdrReadbackBytes_.size() == kGpuHistogramReadbackByteCount;
}

SceneHdrExposureReadbackResult SceneExposureMeter::SubmitHdrReadback(const SceneHdrExposureReadbackDesc& desc) noexcept {
    SceneHdrExposureReadbackResult result{
        .hasValidSample = latestHdrSampleValid_,
        .meteredAverageLuminance = latestHdrAverageLuminance_,
    };
    if (!IsGpuInitialized() || !desc.IsValid()) {
        return result;
    }

    result.sampleAvailable = ConsumeHdrReadback(desc.completedFrame);
    result.hasValidSample = latestHdrSampleValid_;
    result.meteredAverageLuminance = latestHdrAverageLuminance_;
    if (hdrReadbackPending_) {
        return result;
    }

    ConfigureReadbackView(desc.viewId, hdrReadbackFrameBuffer_);
    constexpr float logRange = kHdrReadbackMaxLog2Luminance - kHdrReadbackMinLog2Luminance;
    const float params[4] = {
        static_cast<float>(kHdrReadbackWidth),
        static_cast<float>(kHdrReadbackHeight),
        kHdrReadbackMinLog2Luminance,
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

void SceneExposureMeter::Reset() noexcept {
    adaptedAverageLuminance_ = 0.18F;
    hasHistory_ = false;
    latestHdrAverageLuminance_ = 0.18F;
    latestHdrSampleValid_ = false;
    hdrReadbackReadyFrame_ = 0;
    hdrReadbackPending_ = false;
    std::ranges::fill(hdrReadbackBytes_, 0U);
}

float SceneExposureMeter::Update(float meteredAverageLuminance, SceneExposureAdaptationDesc desc) noexcept {
    const float target = std::clamp(meteredAverageLuminance, kMinAverageLuminance, kMaxAverageLuminance);
    if (!hasHistory_ || !desc.enabled) {
        adaptedAverageLuminance_ = target;
        hasHistory_ = true;
        return adaptedAverageLuminance_;
    }

    const float currentLog = std::log2(std::clamp(adaptedAverageLuminance_, kMinAverageLuminance, kMaxAverageLuminance));
    const float targetLog = std::log2(target);
    const float rate = targetLog > currentLog ? desc.brightAdaptationRate : desc.darkAdaptationRate;
    const float alpha = 1.0F - std::exp(-std::max(rate, 0.0F) * std::max(desc.deltaSeconds, 0.0F));
    adaptedAverageLuminance_ = std::exp2(currentLog + ((targetLog - currentLog) * std::clamp(alpha, 0.0F, 1.0F)));
    return std::clamp(adaptedAverageLuminance_, kMinAverageLuminance, kMaxAverageLuminance);
}

float SceneExposureMeter::Update(const RenderScene& scene, const SceneRenderLightingConfig& lightingConfig, SceneExposureAdaptationDesc desc) noexcept {
    return Update(EstimateAverageLuminance(scene, lightingConfig), desc);
}

float SceneExposureMeter::CurrentLuminance() const noexcept {
    return adaptedAverageLuminance_;
}

bool SceneExposureMeter::HasHistory() const noexcept {
    return hasHistory_;
}

void SceneExposureMeter::DestroyGpuResources() noexcept {
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

bool SceneExposureMeter::ConsumeHdrReadback(std::uint32_t completedFrame) noexcept {
    if (!hdrReadbackPending_ || completedFrame < hdrReadbackReadyFrame_) {
        return false;
    }

    hdrReadbackPending_ = false;
    latestHdrAverageLuminance_ = MeterAverageLuminance(BuildGpuHistogramReadbackHistogram(hdrReadbackBytes_));
    latestHdrSampleValid_ = true;
    return true;
}

bool SceneExposureMeter::CreateHdrReadbackTarget() noexcept {
    constexpr std::uint64_t renderFlags =
        BGFX_TEXTURE_RT |
        BGFX_SAMPLER_U_CLAMP |
        BGFX_SAMPLER_V_CLAMP |
        BGFX_SAMPLER_MIN_POINT |
        BGFX_SAMPLER_MAG_POINT |
        BGFX_SAMPLER_MIP_POINT;
    hdrReadbackRenderTexture_ = bgfx::createTexture2D(
        static_cast<std::uint16_t>(kGpuHistogramReadbackWidth),
        static_cast<std::uint16_t>(kGpuHistogramReadbackHeight),
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
        static_cast<std::uint16_t>(kGpuHistogramReadbackWidth),
        static_cast<std::uint16_t>(kGpuHistogramReadbackHeight),
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
