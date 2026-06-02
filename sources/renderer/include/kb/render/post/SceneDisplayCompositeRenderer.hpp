#pragma once

#include "kb/render/frame/FullscreenTexturePass.hpp"
#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

using SceneDisplayOutputTransform = FullscreenTextureOutputTransform;
using SceneDisplayTonemapOperator = FullscreenTextureTonemapOperator;

struct SceneDisplayCompositeDesc {
    bgfx::ViewId viewId = 0;
    bgfx::TextureHandle hdrColor = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    SceneDisplayOutputTransform outputTransform{};
    std::uint32_t clearRgba = 0x000000FFU;
    bool clearTarget = false;

    [[nodiscard]] bool IsValid() const noexcept;
};

class SceneDisplayCompositeRenderer {
public:
    SceneDisplayCompositeRenderer() = default;
    ~SceneDisplayCompositeRenderer();

    SceneDisplayCompositeRenderer(const SceneDisplayCompositeRenderer&) = delete;
    SceneDisplayCompositeRenderer& operator=(const SceneDisplayCompositeRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const SceneDisplayCompositeDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    FullscreenTexturePass fullscreenPass_;
};

} // namespace kb::render
