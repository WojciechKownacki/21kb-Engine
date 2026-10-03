#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

// What the deferred lighting pass needs to add the accumulated bounce light to the current frame.
struct SceneGiBinding {
    // Resolved bounce light of the previous frame: rgb = radiance, a = view depth of the surface.
    bgfx::TextureHandle accum = BGFX_INVALID_HANDLE;
    // View-projection of the frame that `accum` was resolved for, used to reproject into it.
    std::array<float, 16> accumViewProjection{};
    std::uint32_t frameIndex = 0U;
    // False until a frame has been resolved; the pass then skips the bounce light.
    bool active = false;
};

// Per-viewport screen-space GI state: a copy of the frame's lit HDR colour (the radiance source of the
// gather) and two accumulation targets that ping-pong between "last resolved frame" and "being resolved".
class SceneGiHistory {
public:
    ~SceneGiHistory();

    SceneGiHistory() = default;
    SceneGiHistory(const SceneGiHistory&) = delete;
    SceneGiHistory& operator=(const SceneGiHistory&) = delete;

    // Recreates the textures when the extent or format changes (which also drops the history).
    [[nodiscard]] bool Ensure(RenderExtent extent, bgfx::TextureFormat::Enum format);
    void Shutdown() noexcept;
    [[nodiscard]] SceneGiBinding Binding() const noexcept;

    // Queues a copy of the finished frame colour for this frame's gather.
    void Capture(bgfx::ViewId viewId, bgfx::TextureHandle sourceColor) noexcept;
    [[nodiscard]] bool HasCapturedColor() const noexcept { return captured_; }
    [[nodiscard]] bgfx::TextureHandle LitTexture() const noexcept { return lit_; }
    // The accumulation target that is not the one `Binding()` exposes.
    [[nodiscard]] bgfx::FrameBufferHandle ResolveTarget() const noexcept { return frameBuffers_[1U - readIndex_]; }
    // The resolve target becomes the accumulated frame for `viewProjection`.
    void CommitResolve(const std::array<float, 16>& viewProjection) noexcept;

private:
    bgfx::TextureHandle lit_ = BGFX_INVALID_HANDLE;
    std::array<bgfx::TextureHandle, 2> accum_{ bgfx::TextureHandle{ bgfx::kInvalidHandle }, bgfx::TextureHandle{ bgfx::kInvalidHandle } };
    std::array<bgfx::FrameBufferHandle, 2> frameBuffers_{ bgfx::FrameBufferHandle{ bgfx::kInvalidHandle }, bgfx::FrameBufferHandle{ bgfx::kInvalidHandle } };
    RenderExtent extent_{};
    bgfx::TextureFormat::Enum format_ = bgfx::TextureFormat::Count;
    std::array<float, 16> accumViewProjection_{};
    std::uint32_t frameIndex_ = 0U;
    std::uint32_t readIndex_ = 0U;
    bool captured_ = false;
    bool hasAccum_ = false;
};

} // namespace kb::render
