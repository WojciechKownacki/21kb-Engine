#pragma once

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/frame/RenderViewportDesc.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>

namespace kb::render {

struct RenderSceneTargetBinding {
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle colorTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
    RenderViewportDesc viewport{};

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return viewport.IsValid();
    }
};

struct RenderFinalCompositeTargetBinding {
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    RenderExtent extent{};
    RenderViewportRect outputRect{};
    bool enabled = false;
    bool clearTarget = true;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return !enabled || (extent.IsValid() && (!outputRect.extent.IsValid() || outputRect.IsValid()));
    }
};

struct RenderPostProcessTargetBinding {
    static constexpr std::size_t kMaxBloomPyramidMips = 6U;

    bgfx::FrameBufferHandle selectionMaskFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle selectionMaskTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle bloomFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bloomTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle pingFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle pingTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle motionVectorFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle motionVectorTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle temporalHistoryFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle temporalHistoryTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle previousTemporalHistoryTexture = BGFX_INVALID_HANDLE;
    std::array<bgfx::FrameBufferHandle, 2U> temporalHistoryFrameBuffers{{
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
    }};
    std::array<bgfx::TextureHandle, 2U> temporalHistoryTextures{{
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
    }};
    std::uint8_t temporalHistoryWriteIndex = 0;
    bgfx::FrameBufferHandle combineFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle combineTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle finalFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle finalTexture = BGFX_INVALID_HANDLE;
    std::array<bgfx::FrameBufferHandle, kMaxBloomPyramidMips> bloomMipFrameBuffers{{
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
    }};
    std::array<bgfx::FrameBufferHandle, kMaxBloomPyramidMips> pingMipFrameBuffers{{
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE,
    }};
    std::array<RenderExtent, kMaxBloomPyramidMips> bloomMipExtents{};
    std::uint8_t bloomMipCount = 0;
    RenderExtent extent{};
    bool enabled = false;

    [[nodiscard]] bool IsValid() const noexcept {
        if (!enabled) {
            return true;
        }
        const bool baseValid =
            bgfx::isValid(selectionMaskFrameBuffer) && bgfx::isValid(selectionMaskTexture) &&
            bgfx::isValid(bloomFrameBuffer) && bgfx::isValid(bloomTexture) &&
            bgfx::isValid(pingFrameBuffer) && bgfx::isValid(pingTexture) &&
            bgfx::isValid(motionVectorFrameBuffer) && bgfx::isValid(motionVectorTexture) &&
            bgfx::isValid(temporalHistoryFrameBuffers[0]) && bgfx::isValid(temporalHistoryFrameBuffers[1]) &&
            bgfx::isValid(temporalHistoryTextures[0]) && bgfx::isValid(temporalHistoryTextures[1]) &&
            bgfx::isValid(temporalHistoryFrameBuffer) && bgfx::isValid(temporalHistoryTexture) && bgfx::isValid(previousTemporalHistoryTexture) &&
            bgfx::isValid(combineFrameBuffer) && bgfx::isValid(combineTexture) &&
            bgfx::isValid(finalFrameBuffer) && bgfx::isValid(finalTexture) &&
            extent.IsValid();
        if (!baseValid || bloomMipCount == 0U || bloomMipCount > kMaxBloomPyramidMips) {
            return false;
        }
        for (std::uint8_t mip = 0; mip < bloomMipCount; ++mip) {
            if (!bgfx::isValid(bloomMipFrameBuffers[mip]) || !bgfx::isValid(pingMipFrameBuffers[mip]) ||
                !bloomMipExtents[mip].IsValid()) {
                return false;
            }
        }
        return true;
    }

    void SelectTemporalHistory(std::uint64_t frameIndex) noexcept {
        temporalHistoryWriteIndex = static_cast<std::uint8_t>(frameIndex & 1ULL);
        const std::uint8_t readIndex = static_cast<std::uint8_t>(1U - temporalHistoryWriteIndex);
        temporalHistoryFrameBuffer = temporalHistoryFrameBuffers[temporalHistoryWriteIndex];
        temporalHistoryTexture = temporalHistoryTextures[temporalHistoryWriteIndex];
        previousTemporalHistoryTexture = temporalHistoryTextures[readIndex];
    }
};

struct RenderSceneSubmitDesc {
    struct EditorGridDesc {
        float minorSpacingMeters = 1.0F;
        std::uint32_t majorEvery = 10U;
        bool visible = true;
    };

    struct EditorGizmoDesc {
        std::array<float, 3> targetPosition{0.0F, 0.0F, 0.0F};
        float worldScale = 1.0F;
        int hoveredAxis = -1;
        int draggedAxis = -1;
        std::uint8_t mode = 0U;
        bool visible = false;
    };

    struct EditorSelectionBoxDesc {
        float x = 0.0F;
        float y = 0.0F;
        float width = 0.0F;
        float height = 0.0F;
        bool visible = false;
    };

    RenderSceneTargetBinding target{};
    RenderPostProcessTargetBinding postProcess{};
    RenderFinalCompositeTargetBinding finalComposite{};
    std::optional<SceneRenderCamera> cameraOverride{};
    SceneRenderDrawBudget drawBudget{};
    SceneRenderLightingConfig lightingConfig{};
    SceneRenderMeshPassMode meshPassMode = SceneRenderMeshPassMode::OpaqueAndTransparent;
    std::span<const std::uint64_t> selectedEntityIds{};
    std::span<const std::uint64_t> dirtySceneEntityIds{};
    std::uint32_t clearRgba = 0x000000FFU;
    float clearDepth = SceneDepthPolicy::ClearDepth();
    std::uint8_t clearStencil = 0U;
    bool editorSceneOverlaysEnabled = true;
    bool shadowPassEnabled = true;
    bool postProcessEnabled = true;
    bool selectionMaskEnabled = true;
    bool selectionOutlineEnabled = true;
    bool gpuDrivenRuntimeDispatchEnabled = true;
    bool synchronizeScene = true;
    EditorGridDesc editorGrid{};
    EditorGizmoDesc editorGizmo{};
    EditorSelectionBoxDesc editorSelectionBox{};

    [[nodiscard]] bool IsValid() const noexcept {
        return target.IsValid() && postProcess.IsValid() && finalComposite.IsValid() &&
               (!finalComposite.enabled || !postProcessEnabled || postProcess.enabled) &&
               (meshPassMode == SceneRenderMeshPassMode::OpaqueOnly ||
                meshPassMode == SceneRenderMeshPassMode::OpaqueAndTransparent);
    }
};

} // namespace kb::render
