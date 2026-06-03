#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

struct SceneGizmoPassDesc {
    bgfx::ViewId viewId = 0;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    RenderViewportRect outputRect{};
    const SceneRenderCamera* camera = nullptr;

    [[nodiscard]] bool IsValid() const noexcept;
};

class SceneGizmoPass {
public:
    SceneGizmoPass() = default;
    ~SceneGizmoPass();

    SceneGizmoPass(const SceneGizmoPass&) = delete;
    SceneGizmoPass& operator=(const SceneGizmoPass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const SceneGizmoPassDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout lineLayout_{};
    bool initialized_ = false;
};

} // namespace kb::render
