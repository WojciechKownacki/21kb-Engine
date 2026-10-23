#pragma once

#include "engine/particles/ParticleRenderCapabilities.hpp"
#include "kb/render/MaterialProgramRegistry.hpp"
#include "kb/render/particles/ParticleGpuEmitterSimulation.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace kb::scene {
class Scene;
}

namespace kb::render {

class SceneMeshSubmitter;
class ParticleGpuRenderer;
class RenderScene;

// The post-process chain targets the scene's Post Process materials draw through (Unreal's post-process blendables,
// before the tonemap): one view per material, ping-ponging between the two colour targets.
struct ScenePostProcessMaterialChainDesc {
    std::array<std::uint16_t, 4U> views{};
    std::array<bgfx::FrameBufferHandle, 2U> frameBuffers{ { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE } };
    std::array<bgfx::TextureHandle, 2U> textures{ { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE } };
    std::uint16_t width = 0U;
    std::uint16_t height = 0U;
    bgfx::TextureHandle input = BGFX_INVALID_HANDLE;  // one of `textures`
    bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
};

class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Shutdown();
    // Drains the GPU-simulated particle emitter commands of the scene once per rendered frame.
    [[nodiscard]] bool HasGpuParticleEmitters(std::uint64_t sceneId) const noexcept;
    void SyncGpuParticleEmitters(kb::scene::Scene& scene, float frameDeltaSeconds, std::uint64_t frameIndex,
        const kb::math::DVec3& renderOrigin = {});
    // Runs the emitter simulation for the frame in `viewId`, which must not bind the scene depth as a render target.
    void DispatchGpuParticleEmitters(bgfx::ViewId viewId, const SceneRenderCamera& camera, std::uint32_t viewportWidth, std::uint32_t viewportHeight);
    void ReleaseParticleScene(std::uint64_t sceneId) noexcept;
    // Registers the GPU particle consumer of the scene ahead of its first frame (Renderer::PrepareParticleScene).
    void PrepareGpuParticleConsumer(kb::scene::Scene& scene) noexcept;
    void ReleaseAllParticleScenes() noexcept;
    void Submit(bgfx::ViewId viewId, const RenderScene& renderScene, std::uint32_t viewportWidth, std::uint32_t viewportHeight, const SceneRenderCamera* cameraOverride = nullptr, SceneRenderDrawBudget drawBudget = {}, SceneRenderLightingConfig lightingConfig = {}) const;
    void SubmitMeshPass(
        bgfx::ViewId viewId,
        MeshPassType pass,
        const RenderScene& renderScene,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        const SceneRenderCamera* cameraOverride = nullptr,
        SceneRenderDrawBudget drawBudget = {},
        SceneRenderLightingConfig lightingConfig = {},
        const SceneRenderShadowMapBinding* shadowMap = nullptr,
        std::span<const std::uint64_t> selectedEntityIds = {},
        const SceneGpuDrivenFeatureSupport* gpuDrivenSupportOverride = nullptr,
        bool terrainLayersOnly = false) const;
    // Draws the Post Process materials of `renderScene` (Mesh Renderers whose material is of that domain, in entity
    // order, at most one per view) over `chain.input`; returns the texture holding the result.
    [[nodiscard]] bgfx::TextureHandle SubmitPostProcessMaterials(const RenderScene& renderScene, const ScenePostProcessMaterialChainDesc& chain) const;
    // Draws `indexCount` indices of the screen UI's vertices with the User Interface material `materialAssetId`; false
    // (with a named log line) when the material is not loaded, not of that domain or not cooked for the pass.
    [[nodiscard]] bool SubmitUserInterfaceMaterial(bgfx::ViewId viewId, std::uint64_t materialAssetId, const bgfx::TransientVertexBuffer& vertices,
        const bgfx::TransientIndexBuffer& indices, std::uint32_t firstIndex, std::uint32_t indexCount) const;
    void SetDefaultDrawBudget(SceneRenderDrawBudget drawBudget) noexcept;
    [[nodiscard]] SceneRenderDrawBudget DefaultDrawBudget() const noexcept;
    void SetDefaultLightingConfig(SceneRenderLightingConfig lightingConfig) noexcept;
    [[nodiscard]] SceneRenderLightingConfig DefaultLightingConfig() const noexcept;
    void SetGpuDrivenRuntimeSupport(SceneGpuDrivenFeatureSupport support) noexcept;
    [[nodiscard]] SceneGpuDrivenFeatureSupport GpuDrivenRuntimeSupport() const noexcept;
    void SetGraphShaderCacheRoot(std::string root);
    [[nodiscard]] const std::string& GraphShaderCacheRoot() const noexcept;
    // MAT-72: advance per-frame material time (seconds accumulate, frame index increments) and
    // expose it as the u_time vec4 (time, deltaTime, frameIndex, 0) bound during mesh submit.
    void AdvanceFrameTime(float deltaSeconds) noexcept;
    [[nodiscard]] std::array<float, 4> FrameTimeConstants() const noexcept;
    // MAT-30: graph DynamicParameter node source, bound as u_dynamicParameter for graph shaders.
    void SetDynamicParameter(std::array<float, 4> parameter) noexcept;
    [[nodiscard]] std::array<float, 4> DynamicParameterConstants() const noexcept;
    // MAT-80/#18b: the opaque scene depth texture the transparent pass binds to depth-sampling graph
    // materials (SceneDepth / DepthFade). Set by the renderer before submitting the scene.
    void SetSceneDepthTexture(bgfx::TextureHandle texture) noexcept;
    // MAT-31: opaque scene color snapshot bound to color-sampling graph materials (SceneColor / SceneTexture).
    void SetSceneColorTexture(bgfx::TextureHandle texture) noexcept;
    // The jitter-free view-projections of this frame and of the previous one, between which the motion vector
    // pass writes motion; the pass itself rasterizes with the camera it is submitted with.
    void SetMotionVectorViewProjections(
        const std::array<float, 16>& current, const std::array<float, 16>& previous) noexcept;
    void TickFrame() noexcept;
    // Begins this frame in the per-frame matrix row allocators: the skinning palettes and the previous models the
    // motion vector pass uploads for moved instances. `completedFrame` is the frame the GPU finished.
    [[nodiscard]] bool BeginFrameMatrixRows(
        std::uint64_t frame, std::uint64_t completedFrame) noexcept;
    [[nodiscard]] RenderSkinningPaletteAllocator& SkinningPalettes() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] RenderResourceRegistry& Resources() noexcept;
    [[nodiscard]] const RenderResourceRegistry& Resources() const noexcept;
    [[nodiscard]] SceneRenderResourceMap& ResourceMap() noexcept;
    [[nodiscard]] const SceneRenderResourceMap& ResourceMap() const noexcept;
    [[nodiscard]] SceneRenderSubmitStats ValidateSceneResources(const RenderScene& renderScene) const noexcept;
    [[nodiscard]] SceneRenderSubmitStats ValidateSceneResources(const RenderScene& renderScene, MeshPassType pass) const noexcept;
    [[nodiscard]] SceneRenderDiagnostics ValidateSceneDiagnostics(const RenderScene& renderScene) const;
    [[nodiscard]] SceneRenderDiagnostics ValidateSceneDiagnostics(const RenderScene& renderScene, MeshPassType pass) const;
    [[nodiscard]] SceneRenderSubmitStats LastSubmitStats() const noexcept;
    [[nodiscard]] kb::particles::ParticleGpuVisualAvailability ParticleGpuVisualAvailability() const noexcept;
    [[nodiscard]] std::span<const ParticleGpuEmitterSimulation::Draw> GpuParticleDraws(std::uint64_t sceneId) const noexcept;
    [[nodiscard]] std::uint64_t GpuEmitterAllocatedBytes() const noexcept;
    void SetParticleVolumetricLowQuality(bool lowQuality) noexcept;
    [[nodiscard]] bool ParticleVolumetricLowQuality() const noexcept;
    [[nodiscard]] const SceneRenderDiagnostics& LastDiagnostics() const noexcept;
    [[nodiscard]] MaterialProgramRegistryStats MaterialProgramStats() const noexcept;
    // Draw-time, per-material graph render outcome for the last scene submit: the true GPU vs
    // CPU-fallback material counts (a graph material with no cooked binary renders the builtin
    // flatten and counts as a fallback). Reset at the start of each submit before its passes.
    void ResetGraphMaterialDrawStats() const noexcept;
    [[nodiscard]] std::uint32_t GraphMaterialGpuDrawCount() const noexcept;
    [[nodiscard]] std::uint32_t GraphMaterialCpuFallbackDrawCount() const noexcept;

private:
    RenderResourceRegistry resources_;
    SceneRenderResourceMap resourceMap_;
    RenderSkinningPaletteAllocator skinningPalettes_{
        RenderSkinningPaletteAllocatorDesc{ .matrixCapacityPerFrame = 4096U } };
    // One row per instance of each motion vector draw with a moved instance (all viewports of a frame); the
    // const mesh pass submit allocates and uploads them.
    mutable RenderSkinningPaletteAllocator motionPreviousModels_{
        RenderSkinningPaletteAllocatorDesc{ .matrixCapacityPerFrame = 8192U } };
    std::unique_ptr<SceneMeshSubmitter> meshSubmitter_;
    std::unique_ptr<ParticleGpuRenderer> particleRenderer_;
    mutable std::vector<ParticleGpuMeshDraw> gpuMeshDrawScratch_;
    std::uint64_t gpuParticleSceneId_ = 0U;
    mutable MeshPipelineBuildResult validationPipelineScratch_;
    mutable SceneRenderSubmitStats lastSubmitStats_{};
    mutable SceneRenderDiagnostics lastDiagnostics_{};
    SceneRenderDrawBudget defaultDrawBudget_{};
    SceneRenderLightingConfig defaultLightingConfig_{};
    SceneGpuDrivenFeatureSupport gpuDrivenRuntimeSupport_{};
    std::string graphShaderCacheRoot_;
    bgfx::TextureHandle sceneDepthTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sceneColorTexture_ = BGFX_INVALID_HANDLE;
    std::array<float, 16> motionVectorPreviousViewProjection_{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    std::array<float, 16> motionVectorCurrentViewProjection_{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    float frameTimeSeconds_ = 0.0F;
    float frameDeltaSeconds_ = 0.0F;
    std::uint32_t frameTimeIndex_ = 0U;
    // FrameTimeConstants of the previous frame: where graphs evaluate their previous vertex positions.
    std::array<float, 4> previousFrameTime_{};
    std::array<float, 4> dynamicParameter_{};
    bool particleVolumetricLowQuality_ = false;
    bool initialized_ = false;
};

} // namespace kb::render
