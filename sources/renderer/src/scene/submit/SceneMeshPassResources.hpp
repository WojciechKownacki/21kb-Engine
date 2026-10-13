#pragma once

#include "kb/render/shadow/PointShadowUniforms.hpp"
#include "kb/render/shadow/ShadowCascadeUniforms.hpp"
#include "kb/render/MaterialProgramRegistry.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/resources/RenderSkinningPaletteAllocator.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kb::render {

// What the motion vector pass draws with besides the meshes (Unreal's velocity pass).
struct SceneMotionVectorFrame {
    // The jitter-free view-projections of the previous frame and of this one; the pass rasterizes with this
    // frame's jittered view (the prepass depth) and writes motion between these.
    std::array<float, 16> previousViewProjection{};
    std::array<float, 16> currentViewProjection{};
    // u_time of the previous frame: graphs evaluate their previous vertex positions at it.
    std::array<float, 4> previousFrameTime{};
    // RenderScene::MotionFrame of the submitted scene: which instances recorded a previous model this frame.
    std::uint64_t motionFrame = 0U;
    // Per-frame rows the previous models of moved instances are uploaded to, indexed by instance.
    RenderSkinningPaletteAllocator* previousModels = nullptr;
};

struct SceneMeshPassBindDesc {
    const MeshDrawCommand& command;
    const RenderResourceRegistry& resources;
    const SceneRenderResourceMap& resourceMap;
    MeshPassType pass = MeshPassType::BaseOpaque;
    const PackedSceneLighting& lighting;
    const std::array<float, 4>& cameraPosition;
    const std::array<float, 4>& frameTime;
    const std::array<float, 4>& dynamicParameter;
    const SceneRenderShadowMapBinding* shadowMap = nullptr;
    // MAT-80/#18b: the resolved opaque scene depth texture, bound to graph fragment shaders that sample it
    // (SceneDepth / DepthFade) in the transparent pass. Invalid when unavailable.
    bgfx::TextureHandle sceneDepthTexture = BGFX_INVALID_HANDLE;
    // MAT-31: the opaque scene-color snapshot for graph fragment shaders that sample SceneColor/SceneTexture.
    bgfx::TextureHandle sceneColorTexture = BGFX_INVALID_HANDLE;
    const SceneMotionVectorFrame* motionVectors = nullptr;
    // The previous models of this draw's instances (SceneMotionVectorFrame::previousModels); invalid when
    // none of them moved this frame.
    RenderSkinningPaletteHandle motionPreviousModels{};
    const RenderSkinningPaletteAllocator* skinningPaletteAllocator = nullptr;
    // What material graphs add to render-space positions to read world positions (SceneMeshSubmitter).
    std::array<float, 4> renderOriginOffset{};
};

// One post-process material over the view (Unreal's post-process blendable, before the tonemap): its PostProcess
// program reads `input` as Scene Color and `depth` as Scene Depth.
struct SceneMeshPostProcessDesc {
    bgfx::ViewId viewId = 0U;
    const RenderMaterialResource& material;
    const RenderResourceRegistry& resources;
    const SceneRenderResourceMap& resourceMap;
    bgfx::TextureHandle input = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
    std::array<float, 4> frameTime{};
    std::array<float, 4> dynamicParameter{};
};

struct SceneMeshProgramBindStats {
    std::uint32_t totalBindCount = 0U;
    std::uint32_t graphProgramBindCount = 0U;
    std::uint32_t builtinProgramBindCount = 0U;
    std::uint32_t builtinFallbackBindCount = 0U;
    std::uint32_t programSwitchCount = 0U;
};

// The ACTUAL, draw-time outcome of graph-material rendering for one scene submit,
// deduplicated per distinct material resource (not per bind — a material binds
// across several passes). A material counts as GPU when it bound its cooked GPU
// program in at least one drawn pass (its authored graph rendered); it counts as
// a CPU fallback only when it NEVER bound its GPU program and fell back to the
// builtin path everywhere (e.g. the cooked binary was missing). This is the
// source of truth for graphMaterialGpuCount / graphMaterialCpuFallbackCount —
// resolve-time renderMode only expresses intent, not what actually rendered.
struct SceneMeshGraphMaterialDrawStats {
    std::uint32_t gpuCount = 0U;
    std::uint32_t cpuFallbackCount = 0U;
};

struct SceneMeshPassProgramResolution {
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    MaterialProgramKey key{};
    std::uint64_t materialProgramIdentity = 0U;
    SceneRenderMaterialProgramStatus status = SceneRenderMaterialProgramStatus::None;
    bool graphProgram = false;
    bool fellBackToBuiltin = false;
};

class SceneMeshPassResources {
public:
    [[nodiscard]] bool Initialize();
    void Shutdown();
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bgfx::ProgramHandle Bind(const SceneMeshPassBindDesc& desc) const noexcept;
    // False when the material's PostProcess program is not available (not cooked, or failed to load).
    [[nodiscard]] bool SubmitPostProcessMaterial(const SceneMeshPostProcessDesc& desc) const;
    [[nodiscard]] MaterialProgramRegistryStats ProgramRegistryStats() const noexcept { return programRegistry_.Stats(); }
    void EndFrame(std::uint64_t frameIndex) const;
    [[nodiscard]] SceneLightGridResources& LightGrid() const noexcept { return lightGrid_; }

    void SetGraphShaderCacheRoot(std::string root) { graphShaderCacheRoot_ = std::move(root); }
    [[nodiscard]] SceneMeshPassProgramResolution ResolveMeshPassProgram(
        const RenderMaterialResource* material, MeshPassType pass,
        bool skinned = false) const noexcept;
    [[nodiscard]] SceneMeshPassProgramResolution LastProgramResolution() const noexcept { return lastProgramResolution_; }
    void ResetProgramBindStats() const noexcept;
    [[nodiscard]] SceneMeshProgramBindStats ProgramBindStats() const noexcept { return programBindStats_; }
    // Cleared at the start of every scene submit (see SceneMeshSubmitter::Submit); the per-material
    // GPU/fallback sets then accumulate across that submit's passes so the counts reflect one frame.
    void ResetGraphMaterialDrawStats() const noexcept;
    [[nodiscard]] SceneMeshGraphMaterialDrawStats GraphMaterialDrawStats() const noexcept;

private:
    // The sky reflection of the forward lighting at this sampler stage (the fallback cube when none).
    void BindSkyReflection(const PackedSceneLighting& lighting, std::uint8_t stage) const;
    // The motion vector pass inputs (motion_vectors.sh); false when the draw's previous models are gone.
    [[nodiscard]] bool BindMotionVectors(const SceneMeshPassBindDesc& desc, bool skinned) const;

    struct RetiredGraphUniform {
        bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
        std::uint64_t destroyFrame = 0U;
    };

    [[nodiscard]] bgfx::ProgramHandle LoadProgramForKey(const MaterialProgramKey& key) const;
    // The graph material's own uniforms and textures (its parameters, collections and texture slots).
    void BindGraphMaterialValues(const RenderMaterialResource& material, const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap) const;
    [[nodiscard]] bgfx::TextureHandle GraphFallbackTexture(
        RenderTextureDimension dimension,
        bool normal) const noexcept;
    [[nodiscard]] bgfx::UniformHandle& AcquireGraphUniform(
        std::string_view name,
        bool sampler) const;

    mutable SceneLightGridResources lightGrid_;
    std::string graphShaderCacheRoot_;
    mutable MaterialProgramRegistry programRegistry_;
    mutable std::vector<MaterialProgramKey> residentProgramKeys_;
    mutable std::vector<MaterialProgramKey> usedProgramKeys_;
    mutable std::vector<std::string> usedGraphSamplerUniforms_;
    mutable std::vector<std::string> usedGraphUniforms_;
    mutable SceneMeshProgramBindStats programBindStats_{};
    // Distinct graph-material resources that bound their GPU program vs fell back this submit.
    // Keyed by the material resource pointer, which is stable for the duration of a submit's draw
    // (resources are ensured before drawing and not mutated during it). See GraphMaterialDrawStats().
    mutable std::unordered_set<const void*> graphGpuMaterials_;
    mutable std::unordered_set<const void*> graphFellBackMaterials_;
    mutable bgfx::ProgramHandle lastBoundProgram_ = BGFX_INVALID_HANDLE;
    mutable SceneMeshPassProgramResolution lastProgramResolution_{};
    // MAT-78/#16: per-graph texture sampler uniforms, created lazily by name and reused across binds so the
    // scene actually binds a graph material's own textures (slot >= 6), not just the builtin PBR slots.
    mutable std::unordered_map<std::string, bgfx::UniformHandle> graphSamplerUniforms_;
    mutable std::unordered_map<std::string, RetiredGraphUniform> retiredGraphSamplerUniforms_;
    // Numeric graph uniforms are created lazily by generated shader name. Collection-backed uniforms are
    // refreshed from the global runtime store at bind time so global parameter edits do not recompile graphs.
    mutable std::unordered_map<std::string, bgfx::UniformHandle> graphUniforms_;
    mutable std::unordered_map<std::string, RetiredGraphUniform> retiredGraphUniforms_;
    bgfx::ProgramHandle meshProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle gbufferProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle shadowProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle selectionProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle albedoSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadowSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle skinningPaletteSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle skinningPaletteInfoUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle previousSkinningPaletteSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle previousSkinningPaletteInfoUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle motionDepthSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle motionPreviousViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle motionCurrentViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle motionPreviousModelInfoUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle motionPreviousModelsSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle motionPreviousTimeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle normalSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle metallicRoughnessSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle occlusionSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle emissiveSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle terrainLayerWeightSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle terrainLayerParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle materialParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle materialEmissiveUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle materialFlagsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle materialUvTransformUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle renderOriginOffsetUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle timeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle dynamicParameterUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightDirKindUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightPositionRangeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightColorIntensityUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightSpotUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightAreaRightUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle ambientColorUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentZenithUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentGroundUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle environmentParamsUniform_ = BGFX_INVALID_HANDLE;
    // The sky light's prefiltered cube (sky_reflection.sh) of the forward lighting.
    bgfx::UniformHandle skyReflectionSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle skyReflectionParamsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadowViewProjUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle shadowParamsUniform_ = BGFX_INVALID_HANDLE;
    ShadowCascadeUniforms shadowCascades_{};
    PointShadowUniforms pointShadows_{};
    bgfx::TextureHandle fallbackWhiteTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackNormalTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackWhiteCubeTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackNormalCubeTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackWhite3DTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackNormal3DTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackWhite2DArrayTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackNormal2DArrayTexture_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
