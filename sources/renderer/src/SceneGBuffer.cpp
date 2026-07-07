#include "kb/render/SceneGBuffer.hpp"

#include "renderer/RendererDebugLog.hpp"

#include <algorithm>
#include <sstream>

namespace kb::render {
namespace {

constexpr std::uint64_t kGBufferColorTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_SAMPLER_MIN_POINT |
    BGFX_SAMPLER_MAG_POINT |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

constexpr std::uint64_t kGBufferDepthTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_SAMPLER_MIN_POINT |
    BGFX_SAMPLER_MAG_POINT |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

[[nodiscard]] bool IsSupportedExtent(std::uint32_t width, std::uint32_t height) noexcept {
    return width > 0U && height > 0U && width <= UINT16_MAX && height <= UINT16_MAX;
}

[[nodiscard]] RenderTargetFormat ToRenderTargetFormat(bgfx::TextureFormat::Enum format) noexcept {
    switch (format) {
    case bgfx::TextureFormat::BGRA8:
        return RenderTargetFormat::Bgra8;
    case bgfx::TextureFormat::RGBA8:
        return RenderTargetFormat::Rgba8;
    case bgfx::TextureFormat::RG16F:
        return RenderTargetFormat::Rg16F;
    case bgfx::TextureFormat::RGBA16F:
        return RenderTargetFormat::Rgba16F;
    case bgfx::TextureFormat::D24S8:
        return RenderTargetFormat::D24S8;
    case bgfx::TextureFormat::D32:
        return RenderTargetFormat::D32;
    case bgfx::TextureFormat::D32F:
        return RenderTargetFormat::D32F;
    default:
        return RenderTargetFormat::Unknown;
    }
}

[[nodiscard]] const char* TextureFormatName(bgfx::TextureFormat::Enum format) noexcept {
    switch (format) {
    case bgfx::TextureFormat::BGRA8:
        return "BGRA8";
    case bgfx::TextureFormat::RGBA8:
        return "RGBA8";
    case bgfx::TextureFormat::RG16F:
        return "RG16F";
    case bgfx::TextureFormat::RGBA16F:
        return "RGBA16F";
    case bgfx::TextureFormat::D24S8:
        return "D24S8";
    case bgfx::TextureFormat::D32:
        return "D32";
    case bgfx::TextureFormat::D32F:
        return "D32F";
    case bgfx::TextureFormat::Count:
        return "Count";
    default:
        return "Other";
    }
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::TextureHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::FrameBufferHandle handle) noexcept {
    return handle.idx;
}

} // namespace

bool SceneGBufferFormatSelection::IsSupported() const noexcept {
    return albedoFormat != bgfx::TextureFormat::Count &&
        normalFormat != bgfx::TextureFormat::Count &&
        materialFormat != bgfx::TextureFormat::Count &&
        depth.IsSupported() &&
        status != SceneTargetFormatSelectionStatus::Unsupported;
}

SceneGBuffer::~SceneGBuffer() {
    Shutdown();
}

bool SceneGBuffer::Ensure(const SceneGBufferDesc& desc) {
    if (!desc.IsValid()) {
        return false;
    }
    return Ensure(desc.extent.width, desc.extent.height);
}

bool SceneGBuffer::Ensure(std::uint32_t width, std::uint32_t height) {
    {
        std::ostringstream message;
        message << "Ensure begin requested=" << width << 'x' << height
                << " current=" << width_ << 'x' << height_
                << " valid=" << (IsValid() ? "true" : "false")
                << " renderer=" << static_cast<int>(bgfx::getRendererType());
        WriteRendererDebugLog("gbuffer", message.str());
        WriteRendererMaterialGraphDebugLog("gbuffer", message.str());
    }

    width = std::max(1U, width);
    height = std::max(1U, height);

    if (!IsSupportedExtent(width, height)) {
        std::ostringstream message;
        message << "Ensure unsupported extent " << width << 'x' << height;
        WriteRendererDebugLog("gbuffer", message.str());
        WriteRendererMaterialGraphDebugLog("gbuffer", message.str());
        return false;
    }

    if (IsValid() && width_ == width && height_ == height) {
        WriteRendererDebugLog("gbuffer", "Ensure reuse existing targets");
        WriteRendererMaterialGraphDebugLog("gbuffer", "Ensure reuse existing targets");
        return true;
    }

    WriteRendererDebugLog("gbuffer", "Ensure recreate targets begin");
    WriteRendererMaterialGraphDebugLog("gbuffer", "Ensure recreate targets begin");
    Shutdown();

    selection_ = SelectSceneGBufferFormats(kGBufferColorTextureFlags, kGBufferDepthTextureFlags);
    {
        std::ostringstream message;
        message << "Format selection status=" << static_cast<int>(selection_.status)
                << " albedo=" << TextureFormatName(selection_.albedoFormat)
                << " normal=" << TextureFormatName(selection_.normalFormat)
                << " material=" << TextureFormatName(selection_.materialFormat)
                << " depth=" << TextureFormatName(selection_.depth.format)
                << " depthStatus=" << static_cast<int>(selection_.depth.status);
        WriteRendererDebugLog("gbuffer", message.str());
        WriteRendererMaterialGraphDebugLog("gbuffer", message.str());
    }
    if (!selection_.IsSupported()) {
        WriteRendererDebugLog("gbuffer", "Ensure failed unsupported format selection");
        WriteRendererMaterialGraphDebugLog("gbuffer", "Ensure failed unsupported format selection");
        return false;
    }

    const auto textureWidth = static_cast<std::uint16_t>(width);
    const auto textureHeight = static_cast<std::uint16_t>(height);
    albedoTexture_ = bgfx::createTexture2D(textureWidth, textureHeight, false, 1, selection_.albedoFormat, kGBufferColorTextureFlags);
    if (!bgfx::isValid(albedoTexture_)) {
        WriteRendererDebugLog("gbuffer", "Ensure failed create albedo texture");
        Shutdown();
        return false;
    }
    bgfx::setName(albedoTexture_, "KB GBuffer Albedo");

    normalTexture_ = bgfx::createTexture2D(textureWidth, textureHeight, false, 1, selection_.normalFormat, kGBufferColorTextureFlags);
    if (!bgfx::isValid(normalTexture_)) {
        WriteRendererDebugLog("gbuffer", "Ensure failed create normal texture");
        Shutdown();
        return false;
    }
    bgfx::setName(normalTexture_, "KB GBuffer Normal");

    materialTexture_ = bgfx::createTexture2D(textureWidth, textureHeight, false, 1, selection_.materialFormat, kGBufferColorTextureFlags);
    if (!bgfx::isValid(materialTexture_)) {
        WriteRendererDebugLog("gbuffer", "Ensure failed create material texture");
        Shutdown();
        return false;
    }
    bgfx::setName(materialTexture_, "KB GBuffer Material");

    depthTexture_ = bgfx::createTexture2D(textureWidth, textureHeight, false, 1, selection_.depth.format, kGBufferDepthTextureFlags);
    if (!bgfx::isValid(depthTexture_)) {
        WriteRendererDebugLog("gbuffer", "Ensure failed create depth texture");
        Shutdown();
        return false;
    }
    bgfx::setName(depthTexture_, "KB GBuffer Depth");

    bgfx::Attachment attachments[4]{};
    attachments[0].init(albedoTexture_);
    attachments[1].init(normalTexture_);
    attachments[2].init(materialTexture_);
    attachments[3].init(depthTexture_);
    frameBuffer_ = bgfx::createFrameBuffer(4, attachments, true);
    if (!bgfx::isValid(frameBuffer_)) {
        WriteRendererDebugLog("gbuffer", "Ensure failed create framebuffer");
        Shutdown();
        return false;
    }

    width_ = width;
    height_ = height;
    {
        std::ostringstream message;
        message << "Ensure end ok fb=" << HandleValue(frameBuffer_)
                << " albedoTex=" << HandleValue(albedoTexture_)
                << " normalTex=" << HandleValue(normalTexture_)
                << " materialTex=" << HandleValue(materialTexture_)
                << " depthTex=" << HandleValue(depthTexture_)
                << " extent=" << width_ << 'x' << height_;
        WriteRendererDebugLog("gbuffer", message.str());
        WriteRendererMaterialGraphDebugLog("gbuffer", message.str());
    }
    return true;
}

void SceneGBuffer::Shutdown() {
    if (IsValid() || bgfx::isValid(frameBuffer_) || bgfx::isValid(albedoTexture_) || bgfx::isValid(normalTexture_) ||
        bgfx::isValid(materialTexture_) || bgfx::isValid(depthTexture_)) {
        std::ostringstream message;
        message << "Shutdown fb=" << HandleValue(frameBuffer_)
                << " albedoTex=" << HandleValue(albedoTexture_)
                << " normalTex=" << HandleValue(normalTexture_)
                << " materialTex=" << HandleValue(materialTexture_)
                << " depthTex=" << HandleValue(depthTexture_);
        WriteRendererDebugLog("gbuffer", message.str());
    }
    if (bgfx::isValid(frameBuffer_)) {
        bgfx::destroy(frameBuffer_);
    } else {
        if (bgfx::isValid(depthTexture_)) {
            bgfx::destroy(depthTexture_);
        }
        if (bgfx::isValid(materialTexture_)) {
            bgfx::destroy(materialTexture_);
        }
        if (bgfx::isValid(normalTexture_)) {
            bgfx::destroy(normalTexture_);
        }
        if (bgfx::isValid(albedoTexture_)) {
            bgfx::destroy(albedoTexture_);
        }
    }

    frameBuffer_ = BGFX_INVALID_HANDLE;
    albedoTexture_ = BGFX_INVALID_HANDLE;
    normalTexture_ = BGFX_INVALID_HANDLE;
    materialTexture_ = BGFX_INVALID_HANDLE;
    depthTexture_ = BGFX_INVALID_HANDLE;
    selection_ = {};
    width_ = 0;
    height_ = 0;
}

bgfx::FrameBufferHandle SceneGBuffer::FrameBuffer() const noexcept {
    return frameBuffer_;
}

bgfx::TextureHandle SceneGBuffer::AlbedoTexture() const noexcept {
    return albedoTexture_;
}

bgfx::TextureHandle SceneGBuffer::NormalTexture() const noexcept {
    return normalTexture_;
}

bgfx::TextureHandle SceneGBuffer::MaterialTexture() const noexcept {
    return materialTexture_;
}

bgfx::TextureHandle SceneGBuffer::DepthTexture() const noexcept {
    return depthTexture_;
}

RenderExtent SceneGBuffer::Extent() const noexcept {
    return RenderExtent{
        .width = width_,
        .height = height_,
    };
}

std::uint32_t SceneGBuffer::Width() const noexcept {
    return width_;
}

std::uint32_t SceneGBuffer::Height() const noexcept {
    return height_;
}

bool SceneGBuffer::IsValid() const noexcept {
    return bgfx::isValid(frameBuffer_) &&
        bgfx::isValid(albedoTexture_) &&
        bgfx::isValid(normalTexture_) &&
        bgfx::isValid(materialTexture_) &&
        bgfx::isValid(depthTexture_);
}

SceneGBufferFormatSelection SceneGBuffer::FormatSelection() const noexcept {
    return selection_;
}

RenderTargetDesc SceneGBuffer::AlbedoTargetDesc() const noexcept {
    return RenderTargetDesc{
        .role = RenderTargetRole::GBufferAlbedo,
        .format = ToRenderTargetFormat(selection_.albedoFormat),
        .extent = Extent(),
        .renderable = true,
        .sampled = true,
    };
}

RenderTargetDesc SceneGBuffer::NormalTargetDesc() const noexcept {
    return RenderTargetDesc{
        .role = RenderTargetRole::GBufferNormal,
        .format = ToRenderTargetFormat(selection_.normalFormat),
        .extent = Extent(),
        .renderable = true,
        .sampled = true,
    };
}

RenderTargetDesc SceneGBuffer::MaterialTargetDesc() const noexcept {
    return RenderTargetDesc{
        .role = RenderTargetRole::GBufferMaterial,
        .format = ToRenderTargetFormat(selection_.materialFormat),
        .extent = Extent(),
        .renderable = true,
        .sampled = true,
    };
}

RenderTargetDesc SceneGBuffer::DepthTargetDesc() const noexcept {
    return RenderTargetDesc{
        .role = RenderTargetRole::GBufferDepth,
        .format = ToRenderTargetFormat(selection_.depth.format),
        .extent = Extent(),
        .renderable = true,
        .sampled = true,
    };
}

SceneGBufferFormatSelection SelectSceneGBufferFormats(std::uint64_t colorTextureFlags, std::uint64_t depthTextureFlags) noexcept {
    const bgfx::TextureFormat::Enum albedoFormat = SceneTextureFormatSupported(bgfx::TextureFormat::BGRA8, colorTextureFlags)
        ? bgfx::TextureFormat::BGRA8
        : (SceneTextureFormatSupported(bgfx::TextureFormat::RGBA8, colorTextureFlags) ? bgfx::TextureFormat::RGBA8 : bgfx::TextureFormat::Count);
    const bgfx::TextureFormat::Enum normalFormat = SceneTextureFormatSupported(bgfx::TextureFormat::RGBA16F, colorTextureFlags)
        ? bgfx::TextureFormat::RGBA16F
        : (SceneTextureFormatSupported(bgfx::TextureFormat::RGBA8, colorTextureFlags) ? bgfx::TextureFormat::RGBA8 : bgfx::TextureFormat::Count);
    const bgfx::TextureFormat::Enum materialFormat = SceneTextureFormatSupported(bgfx::TextureFormat::RGBA8, colorTextureFlags)
        ? bgfx::TextureFormat::RGBA8
        : (SceneTextureFormatSupported(bgfx::TextureFormat::BGRA8, colorTextureFlags) ? bgfx::TextureFormat::BGRA8 : bgfx::TextureFormat::Count);
    const SceneDepthFormatSelection depth = SelectSceneDepthFormat(depthTextureFlags);

    const bool supported = albedoFormat != bgfx::TextureFormat::Count &&
        normalFormat != bgfx::TextureFormat::Count &&
        materialFormat != bgfx::TextureFormat::Count &&
        depth.IsSupported();
    return SceneGBufferFormatSelection{
        .albedoFormat = albedoFormat,
        .normalFormat = normalFormat,
        .materialFormat = materialFormat,
        .depth = depth,
        .status = supported ? SceneTargetFormatSelectionStatus::Selected : SceneTargetFormatSelectionStatus::Unsupported,
    };
}

} // namespace kb::render
