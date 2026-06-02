#include "kb/render/post/ScenePostProcessTargets.hpp"

#include <algorithm>

namespace kb::render {
namespace {

constexpr std::uint64_t kPostProcessColorTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

constexpr std::array<const char*, ScenePostProcessTargets::kTargetCount> kTargetNames{
    "KB Editor Selection Mask",
    "KB Post Bloom HDR",
    "KB Post Ping HDR",
    "KB Post Combine HDR",
    "KB Post Final HDR",
};

[[nodiscard]] bool IsSupportedExtent(std::uint32_t width, std::uint32_t height) noexcept {
    return width > 0U && height > 0U && width <= UINT16_MAX && height <= UINT16_MAX;
}

} // namespace

ScenePostProcessTargets::~ScenePostProcessTargets() {
    Shutdown();
}

bool ScenePostProcessTargets::Ensure(const ScenePostProcessTargetsDesc& desc) {
    if (!desc.IsValid()) {
        return false;
    }

    const std::uint32_t width = std::max(1U, desc.extent.width);
    const std::uint32_t height = std::max(1U, desc.extent.height);
    if (!IsSupportedExtent(width, height)) {
        return false;
    }

    if (IsValid() && width_ == width && height_ == height && colorPolicy_ == desc.colorPolicy) {
        return true;
    }

    Shutdown();
    return CreateTargets(width, height, desc.colorPolicy);
}

bool ScenePostProcessTargets::CreateTargets(std::uint32_t width, std::uint32_t height, SceneColorFormatPolicy colorPolicy) {
    colorSelection_ = SelectSceneColorFormat(colorPolicy, kPostProcessColorTextureFlags);
    if (!colorSelection_.IsSupported()) {
        Shutdown();
        return false;
    }

    allocated_ = true;
    for (std::size_t index = 0; index < kTargetCount; ++index) {
        const bgfx::TextureFormat::Enum textureFormat = index == SelectionMask
            ? bgfx::TextureFormat::RGBA8
            : colorSelection_.format;
        textures_[index] = bgfx::createTexture2D(
            static_cast<std::uint16_t>(width),
            static_cast<std::uint16_t>(height),
            false,
            1,
            textureFormat,
            kPostProcessColorTextureFlags);
        if (!bgfx::isValid(textures_[index])) {
            Shutdown();
            return false;
        }
        bgfx::setName(textures_[index], kTargetNames[index]);

        frameBuffers_[index] = bgfx::createFrameBuffer(1, &textures_[index], false);
        if (!bgfx::isValid(frameBuffers_[index])) {
            Shutdown();
            return false;
        }
    }

    width_ = width;
    height_ = height;
    colorPolicy_ = colorPolicy;
    return true;
}

void ScenePostProcessTargets::Shutdown() noexcept {
    if (!allocated_) {
        return;
    }
    for (bgfx::FrameBufferHandle& frameBuffer : frameBuffers_) {
        if (bgfx::isValid(frameBuffer)) {
            bgfx::destroy(frameBuffer);
            frameBuffer = BGFX_INVALID_HANDLE;
        }
    }

    for (bgfx::TextureHandle& texture : textures_) {
        if (bgfx::isValid(texture)) {
            bgfx::destroy(texture);
            texture = BGFX_INVALID_HANDLE;
        }
    }

    colorSelection_ = {};
    colorPolicy_ = SceneColorFormatPolicy::Auto;
    width_ = 0;
    height_ = 0;
    allocated_ = false;
}

bool ScenePostProcessTargets::IsValid() const noexcept {
    if (!allocated_) {
        return false;
    }
    return std::ranges::all_of(textures_, [](bgfx::TextureHandle texture) {
               return bgfx::isValid(texture);
           }) &&
           std::ranges::all_of(frameBuffers_, [](bgfx::FrameBufferHandle frameBuffer) {
               return bgfx::isValid(frameBuffer);
           });
}

RenderExtent ScenePostProcessTargets::Extent() const noexcept {
    return RenderExtent{width_, height_};
}

SceneColorFormatSelection ScenePostProcessTargets::ColorSelection() const noexcept {
    return colorSelection_;
}

RenderPostProcessTargetBinding ScenePostProcessTargets::Binding() const noexcept {
    const bool valid = IsValid();
    return RenderPostProcessTargetBinding{
        .selectionMaskFrameBuffer = frameBuffers_[SelectionMask],
        .selectionMaskTexture = textures_[SelectionMask],
        .bloomFrameBuffer = frameBuffers_[Bloom],
        .bloomTexture = textures_[Bloom],
        .pingFrameBuffer = frameBuffers_[Ping],
        .pingTexture = textures_[Ping],
        .combineFrameBuffer = frameBuffers_[Combine],
        .combineTexture = textures_[Combine],
        .finalFrameBuffer = frameBuffers_[Final],
        .finalTexture = textures_[Final],
        .extent = Extent(),
        .enabled = valid,
    };
}

} // namespace kb::render
