#pragma once

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/SceneRenderer.hpp"

#include <bgfx/bgfx.h>

#include <span>
#include <vector>

namespace kb::render {

class SceneMeshSubmitter {
public:
    [[nodiscard]] bool Initialize();
    void Shutdown();
    [[nodiscard]] SceneRenderSubmitStats Submit(
        bgfx::ViewId viewId,
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        MeshPassType pass = MeshPassType::BaseOpaque,
        const SceneRenderCamera* camera = nullptr,
        SceneRenderDiagnostics* diagnostics = nullptr,
        SceneRenderDrawBudget drawBudget = {},
        SceneRenderLightingConfig lightingConfig = {},
        const SceneRenderShadowMapBinding* shadowMap = nullptr,
        std::span<const std::uint64_t> selectedEntityIds = {}) const;
    [[nodiscard]] static SceneRenderSubmitStats ValidateResourcesInto(
        const RenderScene& renderScene,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        std::vector<SceneRenderDrawGroup>& drawGroupsScratch,
        MeshPipelineBuildResult& pipelineScratch,
        MeshPassType pass = MeshPassType::BaseOpaque,
        const SceneRenderCamera* camera = nullptr,
        SceneRenderDiagnostics* diagnostics = nullptr,
        SceneRenderDrawBudget drawBudget = {},
        SceneRenderLightingConfig lightingConfig = {},
        std::span<const std::uint64_t> selectedEntityIds = {}) noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    bgfx::ProgramHandle meshProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle shadowProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle selectionProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle albedoSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadowSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle normalSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle metallicRoughnessSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle occlusionSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle emissiveSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle materialParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle materialEmissiveUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle materialFlagsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightDirKindUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightPositionRangeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightColorIntensityUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightSpotUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambientColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentZenithUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentGroundUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadowViewProjUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadowParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackWhiteTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackNormalTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackBlackTexture_ = BGFX_INVALID_HANDLE;
    mutable std::vector<SceneRenderDrawGroup> drawGroupsScratch_;
    mutable MeshPipelineBuildResult pipelineScratch_;
};

} // namespace kb::render
