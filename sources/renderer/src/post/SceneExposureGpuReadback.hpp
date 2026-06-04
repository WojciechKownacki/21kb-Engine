#pragma once

#include "kb/render/post/SceneExposureMeter.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

namespace kb::render {

class SceneExposureGpuReadback final {
public:
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] SceneHdrExposureReadbackResult Submit(const SceneHdrExposureReadbackDesc& desc) noexcept;
    void Reset() noexcept;

private:
    void DestroyResources() noexcept;
    [[nodiscard]] bool Consume(std::uint32_t completedFrame) noexcept;
    [[nodiscard]] bool CreateTarget() noexcept;

    bgfx::ProgramHandle hdrLuminanceProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle hdrSourceSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle hdrExposureParams_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout fullscreenLayout_{};
    bgfx::VertexBufferHandle fullscreenVertexBuffer_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle hdrReadbackRenderTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle hdrReadbackTexture_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle hdrReadbackFrameBuffer_ = BGFX_INVALID_HANDLE;
    std::vector<std::uint8_t> hdrReadbackBytes_{};
    std::uint32_t hdrReadbackReadyFrame_ = 0;
    float latestHdrAverageLuminance_ = 0.18F;
    bool hdrReadbackPending_ = false;
    bool latestHdrSampleValid_ = false;
};

} // namespace kb::render
