#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"
#include "kb/render/post/SceneDisplayCompositeRenderer.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

struct FinalCompositePassDesc {
    bgfx::ViewId viewId = 0;
    bgfx::TextureHandle postProcessColor = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    SceneDisplayOutputTransform outputTransform{};
    std::uint32_t clearRgba = 0x000000FFU;
    bool clearTarget = false;

    [[nodiscard]] bool IsValid() const noexcept;
};

class FinalCompositePass {
public:
    FinalCompositePass() = default;
    ~FinalCompositePass();

    FinalCompositePass(const FinalCompositePass&) = delete;
    FinalCompositePass& operator=(const FinalCompositePass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const FinalCompositePassDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    SceneDisplayCompositeRenderer displayComposite_;
};

} // namespace kb::render
