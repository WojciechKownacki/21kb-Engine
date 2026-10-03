#include "kb/render/shadow/ShadowMapResource.hpp"

#include "kb/render/SceneRenderTargetFormat.hpp"

#include <algorithm>

namespace kb::render {

ShadowMapResource::~ShadowMapResource() {
    Shutdown();
}

bool ShadowMapResource::Ensure(std::uint32_t size) {
    return Ensure(size, size);
}

bool ShadowMapResource::Ensure(std::uint32_t width, std::uint32_t height) {
    const std::uint32_t size = std::clamp(width, 64U, 8192U);
    height = std::clamp(height, 64U, 8192U);
    if (bgfx::isValid(frameBuffer_) &&
        bgfx::isValid(depthTexture_) &&
        size_ == size && height_ == height) {
        return true;
    }

    Shutdown();

    constexpr std::uint64_t textureFlags =
        BGFX_TEXTURE_RT |
        BGFX_SAMPLER_MIN_POINT |
        BGFX_SAMPLER_MAG_POINT |
        BGFX_SAMPLER_MIP_POINT |
        BGFX_SAMPLER_U_CLAMP |
        BGFX_SAMPLER_V_CLAMP;
    const SceneDepthFormatSelection depthSelection = SelectSceneDepthFormat(textureFlags);
    if (!depthSelection.IsSupported()) {
        return false;
    }

    depthTexture_ = bgfx::createTexture2D(static_cast<std::uint16_t>(size), static_cast<std::uint16_t>(height), false, 1U, depthSelection.format, textureFlags);
    if (!bgfx::isValid(depthTexture_)) {
        Shutdown();
        return false;
    }
    bgfx::setName(depthTexture_, "KB Shadow Depth");

    frameBuffer_ = bgfx::createFrameBuffer(1U, &depthTexture_, true);
    if (!bgfx::isValid(frameBuffer_)) {
        Shutdown();
        return false;
    }

    size_ = size;
    height_ = height;
    return true;
}

void ShadowMapResource::Shutdown() noexcept {
    if (bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(frameBuffer_);
    } else if (bgfx::isValid(depthTexture_)) {
        bgfx::destroy(depthTexture_);
    }
    frameBuffer_ = BGFX_INVALID_HANDLE;
    depthTexture_ = BGFX_INVALID_HANDLE;
    size_ = 0U;
    height_ = 0U;
}

bgfx::TextureHandle ShadowMapResource::DepthTexture() const noexcept {
    return depthTexture_;
}

bgfx::FrameBufferHandle ShadowMapResource::FrameBuffer() const noexcept {
    return frameBuffer_;
}

std::uint32_t ShadowMapResource::Size() const noexcept {
    return size_;
}

std::uint32_t ShadowMapResource::Height() const noexcept {
    return height_;
}

bool ShadowMapResource::IsAllocated() const noexcept {
    return bgfx::isValid(frameBuffer_) && bgfx::isValid(depthTexture_);
}

std::uint64_t ShadowMapResource::AllocationBytes() const noexcept {
    return AllocationBytesFor(size_, height_);
}

std::uint64_t ShadowMapResource::AllocationBytesFor(std::uint32_t shadowMapSize) noexcept {
    return AllocationBytesFor(shadowMapSize, shadowMapSize);
}

std::uint64_t ShadowMapResource::AllocationBytesFor(std::uint32_t width, std::uint32_t height) noexcept {
    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
}

} // namespace kb::render
