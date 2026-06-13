#pragma once

#include "kb/render/frame/RenderViewportDesc.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

struct SelectionBoxOverlayPassDesc {
    bgfx::ViewId viewId = 0;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    RenderViewportRect outputRect{};
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool visible = false;

    [[nodiscard]] bool IsValid() const noexcept;
};

class SelectionBoxOverlayPass {
public:
    SelectionBoxOverlayPass() = default;
    ~SelectionBoxOverlayPass();

    SelectionBoxOverlayPass(const SelectionBoxOverlayPass&) = delete;
    SelectionBoxOverlayPass& operator=(const SelectionBoxOverlayPass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const SelectionBoxOverlayPassDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout_{};
    bool initialized_ = false;
};

} // namespace kb::render
