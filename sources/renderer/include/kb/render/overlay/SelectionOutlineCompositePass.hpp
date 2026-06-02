#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

struct SelectionOutlineCompositePassDesc {
    bgfx::ViewId viewId = 0;
    bgfx::TextureHandle selectionMask = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};

    [[nodiscard]] bool IsValid() const noexcept;
};

class SelectionOutlineCompositePass {
public:
    SelectionOutlineCompositePass() = default;
    ~SelectionOutlineCompositePass();

    SelectionOutlineCompositePass(const SelectionOutlineCompositePass&) = delete;
    SelectionOutlineCompositePass& operator=(const SelectionOutlineCompositePass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const SelectionOutlineCompositePassDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle selectionMaskSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle outlineParams_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout fullscreenLayout_{};
    bool initialized_ = false;
};

} // namespace kb::render
