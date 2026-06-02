#pragma once

#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/frame/RenderViewportViewIds.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

struct ScenePostProcessSettings {
    float bloomStrength = 0.05F;
    float bloomThreshold = 1.0F;
    float bloomRadiusPixels = 1.5F;
};

struct ScenePostProcessSubmitDesc {
    bgfx::TextureHandle sceneColor = BGFX_INVALID_HANDLE;
    RenderPostProcessTargetBinding target{};
    RenderViewportViewIds viewIds{};
    ScenePostProcessSettings settings{};

    [[nodiscard]] bool IsValid() const noexcept;
};

class ScenePostProcessRenderer {
public:
    ScenePostProcessRenderer() = default;
    ~ScenePostProcessRenderer();

    ScenePostProcessRenderer(const ScenePostProcessRenderer&) = delete;
    ScenePostProcessRenderer& operator=(const ScenePostProcessRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bgfx::TextureHandle Submit(const ScenePostProcessSubmitDesc& desc) const;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    void DestroyPrograms() noexcept;

    bgfx::ProgramHandle prefilterProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle blurProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle combineProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sourceSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle bloomSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle postParams_ = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout fullscreenLayout_{};
    bgfx::VertexBufferHandle fullscreenVertexBuffer_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
