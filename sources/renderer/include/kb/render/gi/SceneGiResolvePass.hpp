#pragma once

#include "kb/render/SceneGBuffer.hpp"
#include "kb/render/gi/SceneGiHistory.hpp"
#include "kb/render/gi/SceneGiUniforms.hpp"
#include "kb/render/gi/SceneGiVoxelGrid.hpp"
#include "kb/render/scene/RenderScene.hpp"
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
    // Voxel GI only: the grid to trace and the scene whose lights shade the voxels that rays hit.
    const SceneGiVoxelGrid* voxels = nullptr;
    const RenderScene* renderScene = nullptr;
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
    bgfx::UniformHandle voxelAlbedoSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle voxelEmissiveSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle voxelGridUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle voxelInfoUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle voxelLightDirKindUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle voxelLightPositionRangeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle voxelLightColorIntensityUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle voxelLightSpotUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle voxelLightFlagsUniform_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
