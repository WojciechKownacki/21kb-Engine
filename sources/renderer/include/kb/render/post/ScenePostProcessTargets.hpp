#pragma once

#include "kb/render/SceneRenderTargetFormat.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

struct ScenePostProcessTargetsDesc {
    RenderExtent extent{};
    SceneColorFormatPolicy colorPolicy = SceneColorFormatPolicy::Auto;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return extent.IsValid();
    }
};

class ScenePostProcessTargets {
public:
    static constexpr std::size_t kTargetCount = 8U;

    ScenePostProcessTargets() = default;
    ~ScenePostProcessTargets();

    ScenePostProcessTargets(const ScenePostProcessTargets&) = delete;
    ScenePostProcessTargets& operator=(const ScenePostProcessTargets&) = delete;

    [[nodiscard]] bool Ensure(const ScenePostProcessTargetsDesc& desc);
    void Shutdown() noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] RenderExtent Extent() const noexcept;
    [[nodiscard]] SceneColorFormatSelection ColorSelection() const noexcept;
    [[nodiscard]] RenderPostProcessTargetBinding Binding() const noexcept;

private:
    enum TargetIndex : std::size_t {
        SelectionMask = 0U,
        Bloom = 1U,
        Ping = 2U,
        MotionVectors = 3U,
        TemporalHistory0 = 4U,
        TemporalHistory1 = 5U,
        Combine = 6U,
        Final = 7U,
    };

    [[nodiscard]] bool CreateTargets(std::uint32_t width, std::uint32_t height, SceneColorFormatPolicy colorPolicy);
    [[nodiscard]] bool CreateBloomPyramidTargets(std::uint32_t width, std::uint32_t height);

    std::array<bgfx::TextureHandle, kTargetCount> textures_{{
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
    }};
    std::array<bgfx::FrameBufferHandle, kTargetCount> frameBuffers_{{
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
    }};
    std::array<bgfx::FrameBufferHandle, RenderPostProcessTargetBinding::kMaxBloomPyramidMips> bloomMipFrameBuffers_{{
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
    }};
    std::array<bgfx::FrameBufferHandle, RenderPostProcessTargetBinding::kMaxBloomPyramidMips> pingMipFrameBuffers_{{
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
    }};
    std::array<RenderExtent, RenderPostProcessTargetBinding::kMaxBloomPyramidMips> bloomMipExtents_{};
    SceneColorFormatSelection colorSelection_{};
    SceneColorFormatPolicy colorPolicy_ = SceneColorFormatPolicy::Auto;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint8_t bloomMipCount_ = 0;
    bool allocated_ = false;
};

} // namespace kb::render
