#pragma once

#include "kb/render/SceneGBuffer.hpp"
#include "kb/render/gi/SceneGiHistory.hpp"
#include "kb/render/gi/SceneGiUniforms.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

struct SceneGiResolvePassDesc {
    bgfx::ViewId viewId = 0;
    SceneGiHistory* history = nullptr;
    const SceneGBuffer* gbuffer = nullptr;
    const SceneRenderCamera* camera = nullptr;
    SceneRenderLightingConfig lightingConfig{};
    RenderExtent extent{};
};

// Gathers this frame's screen-space bounce light and ambient occlusion, blends them with the previous
// frame's and stores the result in the history's resolve target; the deferred lighting pass of the next
// frame applies it.
class SceneGiResolvePass {
public:
    SceneGiResolvePass() = default;
    ~SceneGiResolvePass();

    SceneGiResolvePass(const SceneGiResolvePass&) = delete;
    SceneGiResolvePass& operator=(const SceneGiResolvePass&) = delete;

    // False when the shader is unavailable (e.g. no variant for this backend); screen-space GI then stays off.
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool Submit(const SceneGiResolvePassDesc& desc) const;

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    SceneGiUniforms giUniforms_{};
    bgfx::UniformHandle normalSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle litSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle inverseViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthParamsUniform_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
