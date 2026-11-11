#pragma once

#include "kb/render/scene/SceneBackdropUniforms.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

// The world backdrop of the forward path (gradient, procedural sky or environment map): one fullscreen
// draw at the far plane after the opaque geometry, with the depth test and no depth write, so it covers
// only the pixels no surface was drawn on. It shades with the same function as the background of the
// deferred lighting (sky_backdrop.sh), so both paths show the same sky.
class SceneSkyBackdropPass {
public:
    SceneSkyBackdropPass() = default;
    ~SceneSkyBackdropPass();

    SceneSkyBackdropPass(const SceneSkyBackdropPass&) = delete;
    SceneSkyBackdropPass& operator=(const SceneSkyBackdropPass&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

    // Draws the backdrop into viewId after the opaque geometry submitted there (a sequential view).
    // environment: the equirectangular map of an EnvironmentMap backdrop. False when nothing was drawn
    // (no sky, no camera, or no transient vertices left this frame).
    [[nodiscard]] bool Submit(bgfx::ViewId viewId, const SceneBackdropUniformValues& backdrop,
        bgfx::TextureHandle environment, const SceneRenderCamera* camera);

private:
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle inverseViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle horizonUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle zenithUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle paramsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sunUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sunColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentSampler_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackEnvironment_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
