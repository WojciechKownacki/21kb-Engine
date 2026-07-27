#include "renderer/RendererScreenCapture.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"

#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/file.h>
#include <bx/math.h>

#include <algorithm>
#include <cstddef>

namespace kb::render {
namespace {

[[nodiscard]] std::uint32_t BytesPerPixel(bgfx::TextureFormat::Enum format) noexcept {
    switch (format) {
    case bgfx::TextureFormat::RGBA8:
    case bgfx::TextureFormat::BGRA8:
        return 4U;
    case bgfx::TextureFormat::RGBA16:
    case bgfx::TextureFormat::RGBA16F:
        return 8U;
    default:
        return 0U;
    }
}

[[nodiscard]] std::uint8_t UnitFloatToByte(float value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
}

// Converts one captured pixel row-major buffer to tightly-packed RGBA8. HDR formats are
// clamped to [0,1] - v1 captures the scene color as-is (a linear-clamp snapshot), it does
// not re-run tonemapping on the CPU; documented on the script function's catalog entry.
void ConvertToRgba8(bgfx::TextureFormat::Enum format, const std::vector<std::uint8_t>& source, std::uint32_t pixelCount, std::vector<std::uint8_t>& out) {
    out.resize(static_cast<std::size_t>(pixelCount) * 4U);
    switch (format) {
    case bgfx::TextureFormat::RGBA8:
        std::copy_n(source.begin(), out.size(), out.begin());
        return;
    case bgfx::TextureFormat::BGRA8:
        for (std::uint32_t pixel = 0U; pixel < pixelCount; ++pixel) {
            const std::size_t offset = static_cast<std::size_t>(pixel) * 4U;
            out[offset + 0U] = source[offset + 2U];
            out[offset + 1U] = source[offset + 1U];
            out[offset + 2U] = source[offset + 0U];
            out[offset + 3U] = source[offset + 3U];
        }
        return;
    case bgfx::TextureFormat::RGBA16:
        for (std::uint32_t pixel = 0U; pixel < pixelCount; ++pixel) {
            const std::size_t sourceOffset = static_cast<std::size_t>(pixel) * 8U;
            const std::size_t outOffset = static_cast<std::size_t>(pixel) * 4U;
            for (std::uint32_t channel = 0U; channel < 4U; ++channel) {
                out[outOffset + channel] = source[sourceOffset + (channel * 2U) + 1U];
            }
        }
        return;
    case bgfx::TextureFormat::RGBA16F:
        for (std::uint32_t pixel = 0U; pixel < pixelCount; ++pixel) {
            const std::size_t sourceOffset = static_cast<std::size_t>(pixel) * 8U;
            const std::size_t outOffset = static_cast<std::size_t>(pixel) * 4U;
            for (std::uint32_t channel = 0U; channel < 4U; ++channel) {
                std::uint16_t half = 0U;
                half = static_cast<std::uint16_t>(source[sourceOffset + (channel * 2U)] | (source[sourceOffset + (channel * 2U) + 1U] << 8U));
                out[outOffset + channel] = UnitFloatToByte(bx::halfToFloat(half));
            }
        }
        return;
    default:
        out.assign(out.size(), 0U);
        return;
    }
}

// The blit must be ordered after every pass that writes the captured color texture, so it
// rides the LAST valid view of the submit's plan (bgfx executes blits in view order).
[[nodiscard]] std::uint16_t PickBlitView(const RenderViewportViewIds& viewIds) noexcept {
    const std::uint16_t candidates[] = {
        viewIds.finalComposite,
        viewIds.sceneOverlays,
        viewIds.transparentScene,
        viewIds.opaqueScene,
    };
    for (const std::uint16_t candidate : candidates) {
        if (candidate != ViewId::Invalid) {
            return candidate;
        }
    }
    return ViewId::Invalid;
}

} // namespace

void RendererScreenCapture::Process(const kb::scene::Scene& scene, const RenderSceneSubmitDesc& desc, const RenderViewportViewIds& viewIds, std::uint32_t completedFrame) {
    // Finish an in-flight capture once its readback bytes are valid. Completion is
    // delivered on the captured scene's own next submit - a per-frame game/editor loop
    // reaches this within a frame or two of GPU completion.
    if (inFlight_ && sceneId_ == scene.Id() && completedFrame >= readyFrame_) {
        const bool written = EncodeAndWritePng();
        ReleaseStaging();
        inFlight_ = false;
        kb::scene::SceneRenderFeedback::CompleteScreenCapture(const_cast<kb::scene::Scene&>(scene), requestId_, written);
    }

    const kb::scene::SceneScreenCaptureRequest request = kb::scene::SceneRenderFeedback::PeekScreenCaptureRequest(scene);
    if (request.id == 0U) {
        return;
    }
    if (inFlight_) {
        // One capture in flight globally - leave the request un-consumed so this scene's
        // next submit retries it.
        return;
    }

    const bgfx::Caps* caps = bgfx::getCaps();
    const bool capsSupported = caps != nullptr
        && (caps->supported & BGFX_CAPS_TEXTURE_READ_BACK) != 0U
        && (caps->supported & BGFX_CAPS_TEXTURE_BLIT) != 0U
        && bgfx::getRendererType() != bgfx::RendererType::Noop;
    // An MSAA color target cannot be blitted directly; the capture needs the submit's
    // resolved single-sample texture. Without MSAA the scene color texture itself is the
    // readable source.
    const bgfx::TextureHandle source = desc.target.RequiresColorResolve() ? desc.target.resolvedColorTexture : desc.target.colorTexture;
    const std::uint32_t width = desc.target.viewport.extent.width;
    const std::uint32_t height = desc.target.viewport.extent.height;
    const std::uint32_t bytesPerPixel = BytesPerPixel(desc.target.colorFormat);
    const std::uint16_t blitView = PickBlitView(viewIds);
    const bool sourceSupported = bgfx::isValid(source) && width > 0U && height > 0U && bytesPerPixel != 0U && blitView != ViewId::Invalid;

    kb::scene::SceneRenderFeedback::ConsumeScreenCaptureRequest(const_cast<kb::scene::Scene&>(scene), request.id);
    if (!capsSupported || !sourceSupported) {
        // This submit's configuration can never satisfy the capture (headless Noop, no
        // offscreen color target, unknown format) - report Failed now instead of leaving
        // the request Pending forever.
        kb::scene::SceneRenderFeedback::CompleteScreenCapture(const_cast<kb::scene::Scene&>(scene), request.id, false);
        return;
    }

    staging_ = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        false,
        1U,
        desc.target.colorFormat,
        BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    if (!bgfx::isValid(staging_)) {
        kb::scene::SceneRenderFeedback::CompleteScreenCapture(const_cast<kb::scene::Scene&>(scene), request.id, false);
        return;
    }
    bytes_.resize(static_cast<std::size_t>(width) * height * bytesPerPixel);
    bgfx::blit(blitView, staging_, 0U, 0U, source);
    readyFrame_ = bgfx::readTexture(staging_, bytes_.data());
    inFlight_ = true;
    sceneId_ = scene.Id();
    requestId_ = request.id;
    path_.assign(request.path);
    width_ = width;
    height_ = height;
    format_ = desc.target.colorFormat;
}

void RendererScreenCapture::Shutdown() noexcept {
    ReleaseStaging();
    inFlight_ = false;
    bytes_.clear();
    path_.clear();
}

void RendererScreenCapture::ReleaseScene(kb::scene::Scene& scene) noexcept {
    const kb::scene::SceneScreenCaptureRequest pending =
        kb::scene::SceneRenderFeedback::PeekScreenCaptureRequest(scene);
    if (pending.id != 0U) {
        kb::scene::SceneRenderFeedback::ConsumeScreenCaptureRequest(scene, pending.id);
        kb::scene::SceneRenderFeedback::CompleteScreenCapture(scene, pending.id, false);
    }
    if (!inFlight_ || sceneId_ != scene.Id()) {
        return;
    }
    ReleaseStaging();
    inFlight_ = false;
    kb::scene::SceneRenderFeedback::CompleteScreenCapture(scene, requestId_, false);
    bytes_.clear();
    path_.clear();
    sceneId_ = 0U;
    requestId_ = 0U;
}

bool RendererScreenCapture::EncodeAndWritePng() const {
    std::vector<std::uint8_t> rgba8;
    ConvertToRgba8(format_, bytes_, width_ * height_, rgba8);

    bx::FileWriter writer;
    bx::Error openError;
    if (!bx::open(&writer, path_.c_str(), false, &openError)) {
        return false;
    }
    bx::Error writeError;
    bimg::imageWritePng(&writer, width_, height_, width_ * 4U, rgba8.data(), bimg::TextureFormat::RGBA8, false, &writeError);
    bx::close(&writer);
    return writeError.isOk();
}

void RendererScreenCapture::ReleaseStaging() noexcept {
    if (bgfx::isValid(staging_)) {
        bgfx::destroy(staging_);
        staging_ = BGFX_INVALID_HANDLE;
    }
}

} // namespace kb::render
