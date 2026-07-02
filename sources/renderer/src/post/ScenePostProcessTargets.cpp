#include "kb/render/post/ScenePostProcessTargets.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace kb::render {
namespace {

constexpr std::uint64_t kPostProcessColorTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

constexpr std::array<const char*, ScenePostProcessTargets::kTargetCount> kTargetNames{
    "KB Editor Selection Mask",
    "KB Post Bloom HDR",
    "KB Post Ping HDR",
    "KB Post Motion Vectors",
    "KB Post Temporal History 0",
    "KB Post Temporal History 1",
    "KB Post Combine HDR",
    "KB Post Final HDR",
};

[[nodiscard]] bool IsSupportedExtent(std::uint32_t width, std::uint32_t height) noexcept {
    return width > 0U && height > 0U && width <= UINT16_MAX && height <= UINT16_MAX;
}

[[nodiscard]] std::uint8_t CalculateBloomMipCount(std::uint32_t width, std::uint32_t height) noexcept {
    std::uint8_t count = 1U;
    while (count < RenderPostProcessTargetBinding::kMaxBloomPyramidMips && (width > 1U || height > 1U)) {
        width = std::max(1U, width / 2U);
        height = std::max(1U, height / 2U);
        ++count;
    }
    return count;
}

void WriteBreadcrumb(std::string_view category, std::string_view message) {
    try {
        std::error_code error;
        const std::filesystem::path path = std::filesystem::current_path() / "Saved" / "Logs" / "editor-crash-breadcrumbs.log";
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output{path, std::ios::out | std::ios::app};
        if (!output.is_open()) {
            return;
        }
        const auto now = std::chrono::system_clock::now();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        output << millis
               << " tid=" << static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()))
               << " [" << category << "] " << message << '\n';
        output.flush();
    } catch (...) {
    }
}

} // namespace

ScenePostProcessTargets::~ScenePostProcessTargets() {
    Shutdown();
}

bool ScenePostProcessTargets::Ensure(const ScenePostProcessTargetsDesc& desc) {
    WriteBreadcrumb("post_targets", "Ensure begin");
    if (!desc.IsValid()) {
        WriteBreadcrumb("post_targets", "Ensure invalid desc");
        return false;
    }

    const std::uint32_t width = std::max(1U, desc.extent.width);
    const std::uint32_t height = std::max(1U, desc.extent.height);
    if (!IsSupportedExtent(width, height)) {
        WriteBreadcrumb("post_targets", "Ensure unsupported extent");
        return false;
    }

    if (IsValid() && width_ == width && height_ == height && colorPolicy_ == desc.colorPolicy) {
        WriteBreadcrumb("post_targets", "Ensure already valid");
        return true;
    }

    WriteBreadcrumb("post_targets", "Ensure recreate begin");
    Shutdown();
    const bool created = CreateTargets(width, height, desc.colorPolicy);
    WriteBreadcrumb("post_targets", created ? "Ensure recreate end ok" : "Ensure recreate end failed");
    return created;
}

bool ScenePostProcessTargets::CreateTargets(std::uint32_t width, std::uint32_t height, SceneColorFormatPolicy colorPolicy) {
    WriteBreadcrumb("post_targets", "CreateTargets begin " + std::to_string(width) + "x" + std::to_string(height));
    colorSelection_ = SelectSceneColorFormat(colorPolicy, kPostProcessColorTextureFlags);
    if (!colorSelection_.IsSupported()) {
        WriteBreadcrumb("post_targets", "CreateTargets unsupported color format");
        Shutdown();
        return false;
    }

    allocated_ = true;
    for (std::size_t index = 0; index < kTargetCount; ++index) {
        WriteBreadcrumb("post_targets", "create texture index=" + std::to_string(index));
        const bgfx::TextureFormat::Enum textureFormat = index == SelectionMask
            ? bgfx::TextureFormat::RGBA8
            : (index == MotionVectors ? bgfx::TextureFormat::RGBA16F : colorSelection_.format);
        const bool bloomPyramidTarget = index == Bloom || index == Ping;
        textures_[index] = bgfx::createTexture2D(
            static_cast<std::uint16_t>(width),
            static_cast<std::uint16_t>(height),
            bloomPyramidTarget,
            1,
            textureFormat,
            kPostProcessColorTextureFlags);
        if (!bgfx::isValid(textures_[index])) {
            WriteBreadcrumb("post_targets", "create texture failed index=" + std::to_string(index));
            Shutdown();
            return false;
        }
        WriteBreadcrumb("post_targets", "set texture name begin index=" + std::to_string(index) + " name=" + std::string{kTargetNames[index]});
        bgfx::setName(textures_[index], kTargetNames[index]);
        WriteBreadcrumb("post_targets", "set texture name end index=" + std::to_string(index));

        if (bloomPyramidTarget) {
            WriteBreadcrumb("post_targets", "create bloom framebuffer index=" + std::to_string(index));
            bgfx::Attachment attachment{};
            attachment.init(textures_[index]);
            frameBuffers_[index] = bgfx::createFrameBuffer(1U, &attachment, false);
        } else {
            WriteBreadcrumb("post_targets", "create framebuffer index=" + std::to_string(index));
            frameBuffers_[index] = bgfx::createFrameBuffer(1, &textures_[index], false);
        }
        if (!bgfx::isValid(frameBuffers_[index])) {
            WriteBreadcrumb("post_targets", "create framebuffer failed index=" + std::to_string(index));
            Shutdown();
            return false;
        }
    }

    if (!CreateBloomPyramidTargets(width, height)) {
        WriteBreadcrumb("post_targets", "CreateBloomPyramidTargets failed");
        Shutdown();
        return false;
    }

    width_ = width;
    height_ = height;
    colorPolicy_ = colorPolicy;
    WriteBreadcrumb("post_targets", "CreateTargets end ok");
    return true;
}

bool ScenePostProcessTargets::CreateBloomPyramidTargets(std::uint32_t width, std::uint32_t height) {
    WriteBreadcrumb("post_targets", "CreateBloomPyramidTargets begin");
    bloomMipCount_ = CalculateBloomMipCount(width, height);
    std::uint32_t mipWidth = width;
    std::uint32_t mipHeight = height;
    for (std::uint8_t mip = 0; mip < bloomMipCount_; ++mip) {
        bloomMipExtents_[mip] = RenderExtent{mipWidth, mipHeight};

        bgfx::Attachment bloomAttachment{};
        bloomAttachment.init(textures_[Bloom], bgfx::Access::Write, 0U, 1U, mip);
        WriteBreadcrumb("post_targets", "create bloom mip framebuffer begin mip=" + std::to_string(mip));
        bloomMipFrameBuffers_[mip] = bgfx::createFrameBuffer(1U, &bloomAttachment, false);
        if (!bgfx::isValid(bloomMipFrameBuffers_[mip])) {
            WriteBreadcrumb("post_targets", "create bloom mip framebuffer failed mip=" + std::to_string(mip));
            return false;
        }
        char bloomName[64]{};
        static_cast<void>(std::snprintf(bloomName, sizeof(bloomName), "KB Post Bloom Mip %u", static_cast<unsigned>(mip)));
        WriteBreadcrumb("post_targets", "set bloom mip name begin mip=" + std::to_string(mip));
        bgfx::setName(bloomMipFrameBuffers_[mip], bloomName);
        WriteBreadcrumb("post_targets", "set bloom mip name end mip=" + std::to_string(mip));

        bgfx::Attachment pingAttachment{};
        pingAttachment.init(textures_[Ping], bgfx::Access::Write, 0U, 1U, mip);
        WriteBreadcrumb("post_targets", "create ping mip framebuffer begin mip=" + std::to_string(mip));
        pingMipFrameBuffers_[mip] = bgfx::createFrameBuffer(1U, &pingAttachment, false);
        if (!bgfx::isValid(pingMipFrameBuffers_[mip])) {
            WriteBreadcrumb("post_targets", "create ping mip framebuffer failed mip=" + std::to_string(mip));
            return false;
        }
        char pingName[64]{};
        static_cast<void>(std::snprintf(pingName, sizeof(pingName), "KB Post Bloom Ping Mip %u", static_cast<unsigned>(mip)));
        WriteBreadcrumb("post_targets", "set ping mip name begin mip=" + std::to_string(mip));
        bgfx::setName(pingMipFrameBuffers_[mip], pingName);
        WriteBreadcrumb("post_targets", "set ping mip name end mip=" + std::to_string(mip));

        mipWidth = std::max(1U, mipWidth / 2U);
        mipHeight = std::max(1U, mipHeight / 2U);
    }

    const bool valid = bloomMipCount_ > 0U;
    WriteBreadcrumb("post_targets", valid ? "CreateBloomPyramidTargets end ok" : "CreateBloomPyramidTargets end empty");
    return valid;
}

void ScenePostProcessTargets::Shutdown() noexcept {
    WriteBreadcrumb("post_targets", allocated_ ? "Shutdown begin allocated" : "Shutdown skip not allocated");
    if (!allocated_) {
        return;
    }
    for (bgfx::FrameBufferHandle& frameBuffer : bloomMipFrameBuffers_) {
        if (bgfx::isValid(frameBuffer)) {
            bgfx::destroy(frameBuffer);
            frameBuffer = BGFX_INVALID_HANDLE;
        }
    }
    for (bgfx::FrameBufferHandle& frameBuffer : pingMipFrameBuffers_) {
        if (bgfx::isValid(frameBuffer)) {
            bgfx::destroy(frameBuffer);
            frameBuffer = BGFX_INVALID_HANDLE;
        }
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
    std::ranges::fill(bloomMipExtents_, RenderExtent{});
    bloomMipCount_ = 0;
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
           }) &&
           bloomMipCount_ > 0U &&
           std::ranges::all_of(std::span{bloomMipFrameBuffers_}.first(bloomMipCount_), [](bgfx::FrameBufferHandle frameBuffer) {
               return bgfx::isValid(frameBuffer);
           }) &&
           std::ranges::all_of(std::span{pingMipFrameBuffers_}.first(bloomMipCount_), [](bgfx::FrameBufferHandle frameBuffer) {
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
        .motionVectorFrameBuffer = frameBuffers_[MotionVectors],
        .motionVectorTexture = textures_[MotionVectors],
        .temporalHistoryFrameBuffer = frameBuffers_[TemporalHistory0],
        .temporalHistoryTexture = textures_[TemporalHistory0],
        .previousTemporalHistoryTexture = textures_[TemporalHistory1],
        .temporalHistoryFrameBuffers = {{
            frameBuffers_[TemporalHistory0],
            frameBuffers_[TemporalHistory1],
        }},
        .temporalHistoryTextures = {{
            textures_[TemporalHistory0],
            textures_[TemporalHistory1],
        }},
        .combineFrameBuffer = frameBuffers_[Combine],
        .combineTexture = textures_[Combine],
        .finalFrameBuffer = frameBuffers_[Final],
        .finalTexture = textures_[Final],
        .bloomMipFrameBuffers = bloomMipFrameBuffers_,
        .pingMipFrameBuffers = pingMipFrameBuffers_,
        .bloomMipExtents = bloomMipExtents_,
        .bloomMipCount = bloomMipCount_,
        .extent = Extent(),
        .enabled = valid,
    };
}

} // namespace kb::render
