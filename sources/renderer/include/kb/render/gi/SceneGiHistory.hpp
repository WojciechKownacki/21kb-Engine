#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

// What the deferred lighting pass needs to gather bounced light for the current frame.
struct SceneGiBinding {
    bgfx::TextureHandle history = BGFX_INVALID_HANDLE;
    std::array<float, 16> previousViewProjection{};
    std::uint32_t frameIndex = 0U;
    // False until a previous frame has been captured; the pass then skips the gather.
    bool active = false;
};

// Last frame's lit HDR colour for one viewport, the radiance source of screen-space GI.
class SceneGiHistory {
public:
    ~SceneGiHistory();

    SceneGiHistory() = default;
    SceneGiHistory(const SceneGiHistory&) = delete;
    SceneGiHistory& operator=(const SceneGiHistory&) = delete;

    // Recreates the texture when the extent or format changes (which also drops the history).
    [[nodiscard]] bool Ensure(RenderExtent extent, bgfx::TextureFormat::Enum format);
    void Shutdown() noexcept;
    [[nodiscard]] SceneGiBinding Binding() const noexcept;

    // Queues a copy of the finished frame colour so the next frame can read it.
    void Capture(bgfx::ViewId viewId, bgfx::TextureHandle sourceColor, const std::array<float, 16>& viewProjection) noexcept;

private:
    bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
    RenderExtent extent_{};
    bgfx::TextureFormat::Enum format_ = bgfx::TextureFormat::Count;
    std::array<float, 16> previousViewProjection_{};
    std::uint32_t frameIndex_ = 0U;
    bool captured_ = false;
};

} // namespace kb::render
