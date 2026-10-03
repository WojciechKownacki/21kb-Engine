#include "kb/render/gi/SceneGiResolvePass.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/ShaderLoader.hpp"
#include "renderer/RendererMatrixMath.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace kb::render {
namespace {

struct PosTexVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
};

constexpr std::uint8_t kNormalStage = 1U;
constexpr std::uint8_t kDepthStage = 4U;
constexpr std::uint8_t kLitStage = 9U;
constexpr std::uint8_t kVoxelAlbedoStage = 12U;
constexpr std::uint8_t kVoxelEmissiveStage = 13U;
// The voxel shading loop of ssgi_voxel.sh handles this many lights.
constexpr std::uint16_t kVoxelLightCount = 8U;

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

} // namespace

SceneGiResolvePass::~SceneGiResolvePass() {
    Shutdown();
}

bool SceneGiResolvePass::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    program_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_ssgi_resolve.sc");
    if (!bgfx::isValid(program_)) {
        return false;
    }
    giUniforms_.Create();
    normalSampler_ = bgfx::createUniform("s_gbufferNormal", bgfx::UniformType::Sampler);
    depthSampler_ = bgfx::createUniform("s_gbufferDepth", bgfx::UniformType::Sampler);
    litSampler_ = bgfx::createUniform("s_giLit", bgfx::UniformType::Sampler);
    cameraPositionUniform_ = bgfx::createUniform("u_deferredCameraPosition", bgfx::UniformType::Vec4);
    inverseViewProjectionUniform_ = bgfx::createUniform("u_deferredInverseViewProjection", bgfx::UniformType::Mat4);
    depthParamsUniform_ = bgfx::createUniform("u_deferredDepthParams", bgfx::UniformType::Vec4);
    voxelAlbedoSampler_ = bgfx::createUniform("s_voxelAlbedo", bgfx::UniformType::Sampler);
    voxelEmissiveSampler_ = bgfx::createUniform("s_voxelEmissive", bgfx::UniformType::Sampler);
    voxelGridUniform_ = bgfx::createUniform("u_voxelGrid", bgfx::UniformType::Vec4);
    voxelInfoUniform_ = bgfx::createUniform("u_voxelInfo", bgfx::UniformType::Vec4);
    voxelLightDirKindUniform_ = bgfx::createUniform("u_voxelLightDirKind", bgfx::UniformType::Vec4, kVoxelLightCount);
    voxelLightPositionRangeUniform_ = bgfx::createUniform("u_voxelLightPositionRange", bgfx::UniformType::Vec4, kVoxelLightCount);
    voxelLightColorIntensityUniform_ = bgfx::createUniform("u_voxelLightColorIntensity", bgfx::UniformType::Vec4, kVoxelLightCount);
    voxelLightSpotUniform_ = bgfx::createUniform("u_voxelLightSpot", bgfx::UniformType::Vec4, kVoxelLightCount);
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void SceneGiResolvePass::Shutdown() noexcept {
    giUniforms_.Destroy();
    for (bgfx::UniformHandle* handle : { &normalSampler_, &depthSampler_, &litSampler_, &cameraPositionUniform_,
             &inverseViewProjectionUniform_, &depthParamsUniform_, &voxelAlbedoSampler_,
             &voxelEmissiveSampler_, &voxelGridUniform_, &voxelInfoUniform_, &voxelLightDirKindUniform_,
             &voxelLightPositionRangeUniform_, &voxelLightColorIntensityUniform_, &voxelLightSpotUniform_ }) {
        if (bgfx::isValid(*handle)) {
            bgfx::destroy(*handle);
        }
        *handle = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
}

bool SceneGiResolvePass::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && giUniforms_.IsValid() && bgfx::isValid(normalSampler_) && bgfx::isValid(depthSampler_) &&
        bgfx::isValid(litSampler_) && bgfx::isValid(cameraPositionUniform_) && bgfx::isValid(inverseViewProjectionUniform_) &&
        bgfx::isValid(depthParamsUniform_) && bgfx::isValid(voxelAlbedoSampler_) && bgfx::isValid(voxelEmissiveSampler_) &&
        bgfx::isValid(voxelGridUniform_) && bgfx::isValid(voxelInfoUniform_) && bgfx::isValid(voxelLightDirKindUniform_) &&
        bgfx::isValid(voxelLightPositionRangeUniform_) && bgfx::isValid(voxelLightColorIntensityUniform_) &&
        bgfx::isValid(voxelLightSpotUniform_);
}

bool SceneGiResolvePass::Submit(const SceneGiResolvePassDesc& desc) const {
    if (!IsInitialized() || desc.history == nullptr || desc.gbuffer == nullptr || !desc.gbuffer->IsValid() ||
        desc.camera == nullptr || !desc.extent.IsValid() || !desc.history->HasCapturedColor()) {
        return false;
    }
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    constexpr std::array<PosTexVertex, 3> triangle{
        PosTexVertex{-1.0F, 1.0F, 0.0F, 0.0F, 0.0F},
        PosTexVertex{3.0F, 1.0F, 0.0F, 2.0F, 0.0F},
        PosTexVertex{-1.0F, -3.0F, 0.0F, 0.0F, 2.0F},
    };
    constexpr std::uint32_t vertexCount = static_cast<std::uint32_t>(triangle.size());
    if (bgfx::getAvailTransientVertexBuffer(vertexCount, layout) < vertexCount) {
        return false;
    }
    bgfx::TransientVertexBuffer vertices{};
    bgfx::allocTransientVertexBuffer(&vertices, vertexCount, layout);
    std::memcpy(vertices.data, triangle.data(), sizeof(PosTexVertex) * triangle.size());

    constexpr std::array<float, 16> identity{ 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
    bgfx::setViewName(desc.viewId, "KB GI Resolve");
    bgfx::setViewFrameBuffer(desc.viewId, desc.history->ResolveTarget());
    bgfx::setViewRect(desc.viewId, 0, 0, ClampToViewExtent(desc.extent.width), ClampToViewExtent(desc.extent.height));
    bgfx::setViewTransform(desc.viewId, identity.data(), identity.data());
    bgfx::setViewClear(desc.viewId, BGFX_CLEAR_NONE);

    const std::array<float, 16> viewProjection = RendererMatrixMath::ViewProjection(*desc.camera);
    const std::array<float, 16> inverseViewProjection = RendererMatrixMath::Inverse(viewProjection);
    const std::array<float, 4> cameraPosition = SceneLightingPacker::CameraPosition(desc.camera);
    const std::array<float, 4> depthParams{ SceneDepthPolicy::HomogeneousDepth() ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F };
    bgfx::setUniform(cameraPositionUniform_, cameraPosition.data());
    bgfx::setUniform(inverseViewProjectionUniform_, inverseViewProjection.data());
    bgfx::setUniform(depthParamsUniform_, depthParams.data());
    const SceneGiBinding binding = desc.history->Binding();
    giUniforms_.Set(&binding, desc.lightingConfig, viewProjection, BGFX_INVALID_HANDLE);
    bgfx::setTexture(kNormalStage, normalSampler_, desc.gbuffer->NormalTexture());
    bgfx::setTexture(kDepthStage, depthSampler_, desc.gbuffer->DepthTexture());
    bgfx::setTexture(kLitStage, litSampler_, desc.history->LitTexture());
    // Voxel GI traces the world-space grid and shades what a ray hits with the scene lights.
    const bool voxelGi = desc.lightingConfig.globalIllumination == SceneRenderGlobalIlluminationMode::VoxelGrid &&
        desc.voxels != nullptr && desc.voxels->IsValid() && desc.renderScene != nullptr;
    PackedSceneLighting lighting{};
    if (voxelGi) {
        SceneRenderSubmitStats lightingStats{};
        lighting = SceneLightingPacker::Build(*desc.renderScene, lightingStats, desc.lightingConfig, desc.camera);
    }
    const float voxelLights = voxelGi ? std::min(lighting.params[0], static_cast<float>(kVoxelLightCount)) : 0.0F;
    const std::array<float, 4> voxelInfo{ voxelGi ? 1.0F : 0.0F, voxelLights, static_cast<float>(SceneGiVoxelGrid::kDimension), 0.0F };
    bgfx::setUniform(voxelInfoUniform_, voxelInfo.data());
    bgfx::setUniform(voxelGridUniform_, (voxelGi ? desc.voxels->Origin() : std::array<float, 4>{}).data());
    bgfx::setUniform(voxelLightDirKindUniform_, lighting.dirKind.data(), kVoxelLightCount);
    bgfx::setUniform(voxelLightPositionRangeUniform_, lighting.positionRange.data(), kVoxelLightCount);
    bgfx::setUniform(voxelLightColorIntensityUniform_, lighting.colorIntensity.data(), kVoxelLightCount);
    bgfx::setUniform(voxelLightSpotUniform_, lighting.spot.data(), kVoxelLightCount);
    if (voxelGi) {
        bgfx::setTexture(kVoxelAlbedoStage, voxelAlbedoSampler_, desc.voxels->Albedo());
        bgfx::setTexture(kVoxelEmissiveStage, voxelEmissiveSampler_, desc.voxels->Emissive());
    }
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::submit(desc.viewId, program_);
    desc.history->CommitResolve(viewProjection);
    return true;
}

} // namespace kb::render
