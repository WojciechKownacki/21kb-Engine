#pragma once

#include "kb/render/particles/ParticleRenderBatcher.hpp"
#include "kb/render/particles/ParticleStripRenderer.hpp"
#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"
#include "kb/render/particles/ParticleGpuVisualSimulation.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::render {

struct ParticleGpuSubmitResult {
    bool succeeded = false;
    std::uint32_t drawCalls = 0U;
    std::uint32_t submittedParticles = 0U;
    std::uint32_t droppedParticles = 0U;
    std::uint32_t submittedVolumetricParticles = 0U;
    std::uint64_t volumetricRaymarchSteps = 0U;
    ParticleRenderBatchStatus status = ParticleRenderBatchStatus::InvalidSnapshot;
};

enum class ParticleVolumetricQuality : std::uint8_t { Low, High };

class ParticleGpuRenderer final {
public:
    [[nodiscard]] bool Initialize();
    void Shutdown() noexcept;
    void Warmup(std::uint32_t particleCapacity);
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] kb::particles::ParticleGpuVisualAvailability GpuVisualAvailability() const noexcept;
    void SetVolumetricQuality(ParticleVolumetricQuality quality) noexcept;
    [[nodiscard]] ParticleVolumetricQuality VolumetricQuality() const noexcept;

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
    [[nodiscard]] bool PrepareVisualSimulation(
        bgfx::ViewId viewId,
        const kb::particles::ParticleRenderSnapshot& snapshot) noexcept;
    [[nodiscard]] ParticleStripSubmitResult SubmitStripDraw(bgfx::ViewId viewId, std::uint32_t drawIndex) noexcept;
    // GPU-simulated emitters (see engine/particles/ParticleGpuEmitter.hpp). SyncGpuEmitters drains the
    // queued commands of a scene once per rendered frame; SubmitGpuEmitters dispatches the simulation
    // for that frame (once) and draws every emitter of the scene into the given view.
    [[nodiscard]] bool GpuEmittersReady() const noexcept;
    [[nodiscard]] bool HasGpuEmitters(std::uint64_t sceneId) const noexcept;
    void SyncGpuEmitters(kb::scene::Scene& scene, float frameDeltaSeconds, std::uint64_t frameIndex);
    [[nodiscard]] ParticleGpuSubmitResult SubmitGpuEmitters(
        bgfx::ViewId viewId,
        std::uint64_t sceneId,
        const SceneRenderCamera& camera,
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        bgfx::TextureHandle sceneDepthTexture) noexcept;
    void ReleaseParticleScene(std::uint64_t sceneId) noexcept;
    void ReleaseAllParticleScenes() noexcept;

    [[nodiscard]] const ParticleRenderBatchBuildResult& LastBuild() const noexcept;

private:
    ParticleRenderBatcher batcher_;
    ParticleStripRenderer stripRenderer_;
    ParticleGpuVisualSimulation visualSimulation_;
    ParticleGpuEmitterSimulation gpuEmitters_;
    std::vector<kb::particles::ParticleGpuEmitterCommand> gpuEmitterCommandScratch_;
    std::unordered_map<std::uint64_t, std::uint64_t> gpuEmitterSyncedFrame_;
    std::unordered_map<std::uint64_t, bool> gpuEmitterDispatchPending_;
    ParticleRenderBatchBuildResult lastBuild_{};
    std::vector<std::uint32_t> visualMaskScratch_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle atlasSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sceneDepthSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraBasisUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle emitterParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle featureParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle localBasisUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle depthParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle volumetricParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle whiteTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle streakTexture_ = BGFX_INVALID_HANDLE;
    ParticleVolumetricQuality volumetricQuality_ = ParticleVolumetricQuality::High;
};

} // namespace kb::render
