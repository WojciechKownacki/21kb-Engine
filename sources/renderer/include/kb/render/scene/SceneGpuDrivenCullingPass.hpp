#pragma once

#include "kb/render/scene/SceneGpuDrivenFrameResources.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

namespace kb::render {

struct SceneGpuDrivenCullingPassDesc {
    bgfx::ViewId viewId = 0U;
    SceneGpuDrivenFrameBatch batch{};
    const SceneRenderCamera* camera = nullptr;
    SceneGpuDrivenFeatureState featureState = SceneGpuDrivenFeatureState::Disabled;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return batch.instanceCount > 0U &&
            (featureState == SceneGpuDrivenFeatureState::ComputeCulling ||
             featureState == SceneGpuDrivenFeatureState::IndirectDrawSubmit ||
             featureState == SceneGpuDrivenFeatureState::MeshletSubmit);
    }
};

struct MeshDrawCommand;
struct SceneGpuCulledDraw {
    bgfx::DynamicVertexBufferHandle instances = BGFX_INVALID_HANDLE;
    bgfx::IndirectBufferHandle indirect = BGFX_INVALID_HANDLE;
    std::uint32_t capacity = 0U;
    std::uint64_t uploadBytes = 0U;
    [[nodiscard]] bool IsValid() const noexcept { return bgfx::isValid(instances) && bgfx::isValid(indirect); }
};

// Owns compute output until shutdown; view-local slots prevent pass/camera aliasing.
class SceneGpuDrivenCullingPass {
public:
    SceneGpuDrivenCullingPass() = default;
    SceneGpuDrivenCullingPass(const SceneGpuDrivenCullingPass&) = delete;
    SceneGpuDrivenCullingPass& operator=(const SceneGpuDrivenCullingPass&) = delete;
    ~SceneGpuDrivenCullingPass() { Shutdown(); }
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] SceneRenderSubmitStats Submit(const SceneGpuDrivenCullingPassDesc& desc) const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    // Call once after the entire frame, never between cameras or passes.
    void EndFrame() const noexcept;
    [[nodiscard]] SceneGpuCulledDraw Compact(bgfx::ViewId view, const MeshDrawCommand& command,
        const SceneRenderCamera* camera) const;


private:
    struct DrawSlot {
        bgfx::DynamicVertexBufferHandle input = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle output = BGFX_INVALID_HANDLE;
        bgfx::IndirectBufferHandle indirect = BGFX_INVALID_HANDLE;
        std::uint32_t capacity = 0U;
        std::uint64_t revision = 0U;
        void Destroy() noexcept;
    };
    struct ViewSlots {
        bgfx::ViewId view = 0U;
        std::size_t used = 0U;
        std::vector<DrawSlot> slots;
    };
    mutable std::vector<ViewSlots> drawViews_;
    bgfx::ProgramHandle compactClearProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle compactProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle compactParams_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle clearProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle cullProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle finalizeProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle frustumPlanesUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cullingParamsUniform_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
