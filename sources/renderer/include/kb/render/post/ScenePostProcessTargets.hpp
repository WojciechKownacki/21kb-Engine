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
    static constexpr std::size_t kTargetCount = 4U;

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
        Bloom = 0U,
        Ping = 1U,
        Combine = 2U,
        Final = 3U,
    };

    [[nodiscard]] bool CreateTargets(std::uint32_t width, std::uint32_t height, SceneColorFormatPolicy colorPolicy);

    std::array<bgfx::TextureHandle, kTargetCount> textures_{{
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
    }};
    SceneColorFormatSelection colorSelection_{};
    SceneColorFormatPolicy colorPolicy_ = SceneColorFormatPolicy::Auto;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace kb::render
