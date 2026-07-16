#pragma once

#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/frame/RenderViewportViewIds.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::render {

// LIB-145: the renderer half of the scene's async screen-capture channel
// (kb::scene::SceneRenderFeedback::RequestScreenCapture/ScreenCaptureStatus). Mirrors
// SceneExposureGpuReadback's controlled-async pattern exactly: blit the submit's offscreen
// color texture into a BLIT_DST|READ_BACK staging texture, let bgfx::readTexture report
// the frame number at which the CPU bytes become valid, and only consume them once the
// completed-frame counter reaches it - the CPU never waits on the GPU, a capture simply
// finishes a few frames later. The finished bytes are converted to RGBA8 and written as a
// PNG through bimg::imageWritePng.
//
// One capture is in flight globally (one staging texture, one byte buffer - screenshots
// are rare, so the resources are created per capture and destroyed on completion). A
// request that cannot be started yet stays un-consumed in its scene and is retried on that
// scene's next submit; a request that can NEVER succeed (no readback caps / Noop backend /
// no readable offscreen color target / unknown color format) is completed as Failed
// immediately - an honest terminal answer, not a forever-Pending hang.
class RendererScreenCapture {
public:
    // Called once per SubmitSceneToViewport, after the visibility feedback publish (the
    // call site owns the scene-mutable-during-its-own-submit convention). `completedFrame`
    // is Renderer::lastCompletedFrame_ (bgfx::frame()'s return), the same readiness gate
    // the exposure readback uses.
    void Process(const kb::scene::Scene& scene, const RenderSceneSubmitDesc& desc, const RenderViewportViewIds& viewIds, std::uint32_t completedFrame);
    void Shutdown() noexcept;

private:
    [[nodiscard]] bool EncodeAndWritePng() const;
    void ReleaseStaging() noexcept;

    bool inFlight_ = false;
    std::uint64_t sceneId_ = 0;
    std::uint64_t requestId_ = 0;
    std::string path_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bgfx::TextureFormat::Enum format_ = bgfx::TextureFormat::Count;
    bgfx::TextureHandle staging_ = BGFX_INVALID_HANDLE;
    std::uint32_t readyFrame_ = 0;
    std::vector<std::uint8_t> bytes_;
};

} // namespace kb::render
