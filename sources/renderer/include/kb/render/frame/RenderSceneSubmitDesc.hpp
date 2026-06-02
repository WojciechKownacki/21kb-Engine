#pragma once

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/frame/RenderViewportDesc.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <optional>

namespace kb::render {

struct RenderSceneTargetBinding {
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle colorTexture = BGFX_INVALID_HANDLE;
    RenderViewportDesc viewport{};

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return viewport.IsValid();
    }
};

struct RenderFinalCompositeTargetBinding {
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    bool enabled = false;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return !enabled || extent.IsValid();
    }
};

struct RenderPostProcessTargetBinding {
    bgfx::FrameBufferHandle bloomFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bloomTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle pingFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle pingTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle combineFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle combineTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle finalFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle finalTexture = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    bool enabled = false;

    [[nodiscard]] bool IsValid() const noexcept {
        return !enabled || (
            bgfx::isValid(bloomFrameBuffer) && bgfx::isValid(bloomTexture) &&
            bgfx::isValid(pingFrameBuffer) && bgfx::isValid(pingTexture) &&
            bgfx::isValid(combineFrameBuffer) && bgfx::isValid(combineTexture) &&
            bgfx::isValid(finalFrameBuffer) && bgfx::isValid(finalTexture) &&
            extent.IsValid());
    }
};

struct RenderSceneSubmitDesc {
    RenderSceneTargetBinding target{};
    RenderPostProcessTargetBinding postProcess{};
    RenderFinalCompositeTargetBinding finalComposite{};
    std::optional<SceneRenderCamera> cameraOverride{};
    SceneRenderDrawBudget drawBudget{};
    SceneRenderLightingConfig lightingConfig{};
    SceneRenderMeshPassMode meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent;
    std::uint32_t clearRgba = 0x000000FFU;
    float clearDepth = SceneDepthPolicy::ClearDepth();
    std::uint8_t clearStencil = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return target.IsValid() && postProcess.IsValid() && finalComposite.IsValid() &&
               (!finalComposite.enabled || postProcess.enabled) &&
               (meshPassMode == SceneRenderMeshPassMode::OpaqueOnly ||
                meshPassMode == SceneRenderMeshPassMode::OpaqueAndTransparent);
    }
};

} // namespace kb::render
