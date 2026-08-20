#pragma once

#include "kb/render/particles/ParticleRenderBatcher.hpp"
#include "kb/render/particles/ParticleStripRenderer.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace kb::render {

struct ParticleGpuSubmitResult {
    bool succeeded = false;
    std::uint32_t drawCalls = 0U;
    std::uint32_t submittedParticles = 0U;
    std::uint32_t droppedParticles = 0U;
    ParticleRenderBatchStatus status = ParticleRenderBatchStatus::InvalidSnapshot;
};

class ParticleGpuRenderer final {
public:
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    void Warmup(std::uint32_t particleCapacity);
    [[nodiscard]] bool IsInitialized() const noexcept;

    [[nodiscard]] ParticleGpuSubmitResult Submit(
        bgfx::ViewId viewId,
        const kb::particles::ParticleRenderSnapshot& snapshot,
        const SceneRenderCamera& camera,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        bgfx::TextureHandle sceneDepthTexture) noexcept;
    [[nodiscard]] const ParticleRenderBatchBuildResult& Build(
        const kb::particles::ParticleRenderSnapshot& snapshot,
        const SceneRenderCamera& camera) noexcept;
    [[nodiscard]] ParticleGpuSubmitResult SubmitBatch(
        bgfx::ViewId viewId,
        std::uint32_t batchIndex,
        const kb::particles::ParticleRenderSnapshot& snapshot,
        const SceneRenderCamera& camera,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        bgfx::TextureHandle sceneDepthTexture) noexcept;
    [[nodiscard]] const ParticleStripBuildResult& BuildStrips(
        const kb::particles::ParticleRenderSnapshot& snapshot,
        const SceneRenderCamera& camera) noexcept;
    [[nodiscard]] ParticleStripSubmitResult SubmitStripDraw(bgfx::ViewId viewId, std::uint32_t drawIndex) noexcept;
    void ReleaseParticleScene(std::uint64_t sceneId) noexcept;
    void ReleaseAllParticleScenes() noexcept;

    [[nodiscard]] const ParticleRenderBatchBuildResult& LastBuild() const noexcept;

private:
    ParticleRenderBatcher batcher_;
    ParticleStripRenderer stripRenderer_;
    ParticleRenderBatchBuildResult lastBuild_{};
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle atlasSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sceneDepthSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraBasisUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle emitterParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle featureParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle localBasisUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteTexture_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
