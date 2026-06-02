#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

struct SceneGridPassDesc {
    bgfx::ViewId viewId = 0;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    const SceneRenderCamera* camera = nullptr;

    [[nodiscard]] bool IsValid() const noexcept;
};

class SceneGridPass {
public:
    SceneGridPass() = default;
    ~SceneGridPass();

    SceneGridPass(const SceneGridPass&) = delete;
    SceneGridPass& operator=(const SceneGridPass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool Submit(const SceneGridPassDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout lineLayout_{};
    bool initialized_ = false;
};

} // namespace kb::render
