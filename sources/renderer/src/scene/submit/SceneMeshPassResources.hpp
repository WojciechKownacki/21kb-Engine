#pragma once

#include "kb/render/MaterialProgramRegistry.hpp"
#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <string>
#include <unordered_map>

namespace kb::render {

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
};

struct SceneMeshProgramBindStats {
    std::uint32_t totalBindCount = 0U;
    std::uint32_t graphProgramBindCount = 0U;
    std::uint32_t builtinProgramBindCount = 0U;
    std::uint32_t builtinFallbackBindCount = 0U;
    std::uint32_t programSwitchCount = 0U;
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
    [[nodiscard]] MaterialProgramRegistryStats ProgramRegistryStats() const noexcept { return programRegistry_.Stats(); }

    void SetGraphShaderCacheRoot(std::string root) { graphShaderCacheRoot_ = std::move(root); }
    [[nodiscard]] SceneMeshPassProgramResolution ResolveMeshPassProgram(const RenderMaterialResource* material, MeshPassType pass) const noexcept;
    [[nodiscard]] SceneMeshPassProgramResolution LastProgramResolution() const noexcept { return lastProgramResolution_; }
    void ResetProgramBindStats() const noexcept;
    [[nodiscard]] SceneMeshProgramBindStats ProgramBindStats() const noexcept { return programBindStats_; }

private:
    [[nodiscard]] bgfx::ProgramHandle LoadProgramForKey(const MaterialProgramKey& key) const;
    [[nodiscard]] bgfx::TextureHandle GraphFallbackTexture(
        RenderTextureDimension dimension,
        bool normal) const noexcept;

    std::string graphShaderCacheRoot_;
    mutable MaterialProgramRegistry programRegistry_;
    mutable SceneMeshProgramBindStats programBindStats_{};
    mutable bgfx::ProgramHandle lastBoundProgram_ = BGFX_INVALID_HANDLE;
    mutable SceneMeshPassProgramResolution lastProgramResolution_{};
    // MAT-78/#16: per-graph texture sampler uniforms, created lazily by name and reused across binds so the
    // scene actually binds a graph material's own textures (slot >= 6), not just the builtin PBR slots.
    mutable std::unordered_map<std::string, bgfx::UniformHandle> graphSamplerUniforms_;
    // Numeric graph uniforms are created lazily by generated shader name. Collection-backed uniforms are
    // refreshed from the global runtime store at bind time so global parameter edits do not recompile graphs.
    mutable std::unordered_map<std::string, bgfx::UniformHandle> graphUniforms_;
    bgfx::ProgramHandle meshProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle gbufferProgram_ = BGFX_INVALID_HANDLE;
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
    bgfx::UniformHandle materialUvTransformUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle timeUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle dynamicParameterUniform_ = BGFX_INVALID_HANDLE;
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
    bgfx::TextureHandle fallbackWhiteCubeTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackNormalCubeTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackWhite3DTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackNormal3DTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackWhite2DArrayTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallbackNormal2DArrayTexture_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
