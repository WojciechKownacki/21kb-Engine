#include "kb/render/SceneFramebuffer.hpp"

#include <algorithm>

namespace kb::render {
namespace {

constexpr std::uint64_t kSceneColorTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

constexpr std::uint64_t kResolvedSceneColorTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_TEXTURE_BLIT_DST |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

constexpr std::uint64_t kSceneDepthTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_SAMPLER_MIN_POINT |
    BGFX_SAMPLER_MAG_POINT |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

[[nodiscard]] std::uint64_t MsaaTextureFlags(std::uint8_t samples) noexcept {
    switch (samples) {
    case 2U:
        return BGFX_TEXTURE_RT_MSAA_X2;
    case 4U:
        return BGFX_TEXTURE_RT_MSAA_X4;
    case 8U:
        return BGFX_TEXTURE_RT_MSAA_X8;
    case 16U:
        return BGFX_TEXTURE_RT_MSAA_X16;
    default:
        return 0U;
    }
}

[[nodiscard]] std::uint8_t NormalizeMsaaSamples(std::uint8_t samples) noexcept {
    return MsaaTextureFlags(samples) == 0U ? 0U : samples;
}

[[nodiscard]] bool IsSupportedExtent(std::uint32_t width, std::uint32_t height) noexcept {
    return width > 0U && height > 0U && width <= UINT16_MAX && height <= UINT16_MAX;
}

[[nodiscard]] bool ColorFormatSupportedForSceneTarget(
    bgfx::TextureFormat::Enum format,
    std::uint64_t renderFlags,
    std::uint64_t resolveFlags,
    bool needsResolve) noexcept {
    return SceneColorFormatSupported(format, renderFlags) &&
        (!needsResolve || SceneColorFormatSupported(format, resolveFlags));
}

[[nodiscard]] SceneColorFormatSelection SelectSceneFramebufferColorFormat(
    SceneColorFormatPolicy policy,
    std::uint64_t renderFlags,
    std::uint64_t resolveFlags,
    bool needsResolve) noexcept {
    if (policy != SceneColorFormatPolicy::Auto) {
        const bgfx::TextureFormat::Enum requested = SceneColorFormatForPolicy(policy);
        if (ColorFormatSupportedForSceneTarget(requested, renderFlags, resolveFlags, needsResolve)) {
            return SceneColorFormatSelection{requested, policy, SceneTargetFormatSelectionStatus::Selected};
        }
        return SceneColorFormatSelection{bgfx::TextureFormat::Count, policy, SceneTargetFormatSelectionStatus::Unsupported};
    }

    if (ColorFormatSupportedForSceneTarget(bgfx::TextureFormat::RGBA16F, renderFlags, resolveFlags, needsResolve)) {
        return SceneColorFormatSelection{bgfx::TextureFormat::RGBA16F, policy, SceneTargetFormatSelectionStatus::Selected};
    }
    if (ColorFormatSupportedForSceneTarget(bgfx::TextureFormat::RGBA16, renderFlags, resolveFlags, needsResolve)) {
        return SceneColorFormatSelection{bgfx::TextureFormat::RGBA16, policy, SceneTargetFormatSelectionStatus::CapabilityFallback};
    }
    if (ColorFormatSupportedForSceneTarget(bgfx::TextureFormat::RGBA8, renderFlags, resolveFlags, needsResolve)) {
        return SceneColorFormatSelection{bgfx::TextureFormat::RGBA8, policy, SceneTargetFormatSelectionStatus::CapabilityFallback};
    }
    return SceneColorFormatSelection{bgfx::TextureFormat::Count, policy, SceneTargetFormatSelectionStatus::Unsupported};
}

} // namespace

SceneFramebuffer::~SceneFramebuffer() {
    Shutdown();
}

bool SceneFramebuffer::Ensure(std::uint32_t width, std::uint32_t height, SceneColorFormatPolicy colorPolicy, std::uint8_t msaaSamples) {
    width = std::max(1U, width);
    height = std::max(1U, height);
    msaaSamples = NormalizeMsaaSamples(msaaSamples);

    if (!IsSupportedExtent(width, height)) {
        return false;
    }

    if (IsValid() && width_ == width && height_ == height && colorPolicy_ == colorPolicy && msaaSamples_ == msaaSamples) {
        return true;
    }

    Shutdown();

    const std::uint64_t msaaFlags = MsaaTextureFlags(msaaSamples);
    const std::uint64_t colorFlags = kSceneColorTextureFlags | msaaFlags;
    const bool needsResolve = msaaSamples > 0U;
    const std::uint64_t depthFlags = kSceneDepthTextureFlags | msaaFlags | (msaaSamples > 0U ? BGFX_TEXTURE_MSAA_SAMPLE : 0U);
    colorSelection_ = SelectSceneFramebufferColorFormat(colorPolicy, colorFlags, kResolvedSceneColorTextureFlags, needsResolve);
    depthSelection_ = SelectSceneDepthFormat(depthFlags);
    if (!colorSelection_.IsSupported() || !depthSelection_.IsSupported()) {
        return false;
    }

    const auto textureWidth = static_cast<std::uint16_t>(width);
    const auto textureHeight = static_cast<std::uint16_t>(height);
    colorTexture_ = bgfx::createTexture2D(textureWidth, textureHeight, false, 1, colorSelection_.format, colorFlags);
    if (!bgfx::isValid(colorTexture_)) {
        Shutdown();
        return false;
    }
    bgfx::setName(colorTexture_, "KB Scene Color");

    if (needsResolve) {
        resolvedColorTexture_ = bgfx::createTexture2D(textureWidth, textureHeight, false, 1, colorSelection_.format, kResolvedSceneColorTextureFlags);
        if (!bgfx::isValid(resolvedColorTexture_)) {
            Shutdown();
            return false;
        }
        bgfx::setName(resolvedColorTexture_, "KB Scene Color Resolved");
    }

    depthTexture_ = bgfx::createTexture2D(textureWidth, textureHeight, false, 1, depthSelection_.format, depthFlags);
    if (!bgfx::isValid(depthTexture_)) {
        Shutdown();
        return false;
    }
    bgfx::setName(depthTexture_, "KB Scene Depth");

    bgfx::Attachment attachments[2]{};
    attachments[0].init(colorTexture_);
    attachments[1].init(depthTexture_);
    frameBuffer_ = bgfx::createFrameBuffer(2, attachments, true);
    if (!bgfx::isValid(frameBuffer_)) {
        Shutdown();
        return false;
    }

    width_ = width;
    height_ = height;
    colorPolicy_ = colorPolicy;
    msaaSamples_ = msaaSamples;
    return true;
}

void SceneFramebuffer::Shutdown() {
    if (bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(frameBuffer_);
    } else {
        if (bgfx::isValid(depthTexture_)) {
            bgfx::destroy(depthTexture_);
        }
        if (bgfx::isValid(colorTexture_)) {
            bgfx::destroy(colorTexture_);
        }
    }
    if (bgfx::isValid(resolvedColorTexture_)) {
        bgfx::destroy(resolvedColorTexture_);
    }

    frameBuffer_ = BGFX_INVALID_HANDLE;
    colorTexture_ = BGFX_INVALID_HANDLE;
    resolvedColorTexture_ = BGFX_INVALID_HANDLE;
    depthTexture_ = BGFX_INVALID_HANDLE;
    colorSelection_ = {};
    depthSelection_ = {};
    colorPolicy_ = SceneColorFormatPolicy::Auto;
    width_ = 0;
    height_ = 0;
    msaaSamples_ = 0U;
}

bgfx::FrameBufferHandle SceneFramebuffer::FrameBuffer() const noexcept {
    return frameBuffer_;
}

bgfx::TextureHandle SceneFramebuffer::ColorTexture() const noexcept {
    return colorTexture_;
}

bgfx::TextureHandle SceneFramebuffer::ResolvedColorTexture() const noexcept {
    return resolvedColorTexture_;
}

bgfx::TextureHandle SceneFramebuffer::DepthTexture() const noexcept {
    return depthTexture_;
}

std::uint32_t SceneFramebuffer::Width() const noexcept {
    return width_;
}

std::uint32_t SceneFramebuffer::Height() const noexcept {
    return height_;
}

std::uint8_t SceneFramebuffer::MsaaSamples() const noexcept {
    return msaaSamples_;
}

bool SceneFramebuffer::IsValid() const noexcept {
    return bgfx::isValid(frameBuffer_) && bgfx::isValid(colorTexture_) && bgfx::isValid(depthTexture_) &&
        (msaaSamples_ == 0U || bgfx::isValid(resolvedColorTexture_));
}

bool SceneFramebuffer::DepthTextureSampled() const noexcept {
    return true;
}

SceneColorFormatSelection SceneFramebuffer::ColorSelection() const noexcept {
    return colorSelection_;
}

SceneDepthFormatSelection SceneFramebuffer::DepthSelection() const noexcept {
    return depthSelection_;
}

} // namespace kb::render
