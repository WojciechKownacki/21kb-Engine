#pragma once

#include "kb/render/scene/SceneGpuDrivenFrameResources.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

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

class SceneGpuDrivenCullingPass {
public:
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    [[nodiscard]] SceneRenderSubmitStats Submit(const SceneGpuDrivenCullingPassDesc& desc) const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    bgfx::ProgramHandle clearProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle cullProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle finalizeProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle frustumPlanesUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cullingParamsUniform_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
