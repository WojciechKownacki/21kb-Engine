#include "kb/render/SceneDeferredLightingPass.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace kb::render {
namespace {

struct PosTexVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
};

[[nodiscard]] bgfx::VertexLayout FullscreenVertexLayout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

[[nodiscard]] std::array<float, 16> IdentityMatrix() noexcept {
    return std::array<float, 16>{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

} // namespace

SceneDeferredLightingPass::~SceneDeferredLightingPass() {
    Shutdown();
}

bool SceneDeferredLightingPassDesc::IsValid() const noexcept {
    return gbuffer != nullptr && gbuffer->IsValid() && renderScene != nullptr && extent.IsValid();
}

bool SceneDeferredLightingPass::Initialize() {
    if (IsInitialized()) {
        return true;
    }

    program_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_deferred_lighting.sc");
    albedoSampler_ = bgfx::createUniform("s_gbufferAlbedo", bgfx::UniformType::Sampler);
    normalSampler_ = bgfx::createUniform("s_gbufferNormal", bgfx::UniformType::Sampler);
    materialSampler_ = bgfx::createUniform("s_gbufferMaterial", bgfx::UniformType::Sampler);
    lightDirKindUniform_ = bgfx::createUniform("u_lightDirKind", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightColorIntensityUniform_ = bgfx::createUniform("u_lightColorIntensity", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightParamsUniform_ = bgfx::createUniform("u_lightParams", bgfx::UniformType::Vec4);
    ambientColorUniform_ = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
    environmentZenithUniform_ = bgfx::createUniform("u_environmentZenith", bgfx::UniformType::Vec4);
    environmentGroundUniform_ = bgfx::createUniform("u_environmentGround", bgfx::UniformType::Vec4);
    environmentParamsUniform_ = bgfx::createUniform("u_environmentParams", bgfx::UniformType::Vec4);
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void SceneDeferredLightingPass::Shutdown() noexcept {
    if (bgfx::isValid(environmentParamsUniform_)) {
        bgfx::destroy(environmentParamsUniform_);
        environmentParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentGroundUniform_)) {
        bgfx::destroy(environmentGroundUniform_);
        environmentGroundUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(environmentZenithUniform_)) {
        bgfx::destroy(environmentZenithUniform_);
        environmentZenithUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(ambientColorUniform_)) {
        bgfx::destroy(ambientColorUniform_);
        ambientColorUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightParamsUniform_)) {
        bgfx::destroy(lightParamsUniform_);
        lightParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightColorIntensityUniform_)) {
        bgfx::destroy(lightColorIntensityUniform_);
        lightColorIntensityUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightDirKindUniform_)) {
        bgfx::destroy(lightDirKindUniform_);
        lightDirKindUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialSampler_)) {
        bgfx::destroy(materialSampler_);
        materialSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(normalSampler_)) {
        bgfx::destroy(normalSampler_);
        normalSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(albedoSampler_)) {
        bgfx::destroy(albedoSampler_);
        albedoSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(program_)) {
        bgfx::destroy(program_);
        program_ = BGFX_INVALID_HANDLE;
    }
}

bool SceneDeferredLightingPass::Submit(const SceneDeferredLightingPassDesc& desc, SceneRenderSubmitStats& stats) const {
    if (!IsInitialized() || !desc.IsValid()) {
        return false;
    }

    const bgfx::VertexLayout layout = FullscreenVertexLayout();
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

    const std::array<float, 16> identity = IdentityMatrix();
    bgfx::setViewName(desc.viewId, "KB Deferred Lighting");
    bgfx::setViewFrameBuffer(desc.viewId, desc.frameBuffer);
    bgfx::setViewRect(desc.viewId, 0, 0, ClampToViewExtent(desc.extent.width), ClampToViewExtent(desc.extent.height));
    bgfx::setViewTransform(desc.viewId, identity.data(), identity.data());
    bgfx::setViewClear(desc.viewId, BGFX_CLEAR_COLOR, desc.clearRgba);
    bgfx::touch(desc.viewId);

    SceneRenderSubmitStats lightingStats{};
    const PackedSceneLighting lighting = SceneLightingPacker::Build(*desc.renderScene, lightingStats, desc.lightingConfig, desc.camera);
    bgfx::setUniform(lightDirKindUniform_, lighting.dirKind.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightColorIntensityUniform_, lighting.colorIntensity.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightParamsUniform_, lighting.params.data());
    bgfx::setUniform(ambientColorUniform_, lighting.ambient.data());
    bgfx::setUniform(environmentZenithUniform_, lighting.environmentZenith.data());
    bgfx::setUniform(environmentGroundUniform_, lighting.environmentGround.data());
    bgfx::setUniform(environmentParamsUniform_, lighting.environmentParams.data());
    bgfx::setTexture(0U, albedoSampler_, desc.gbuffer->AlbedoTexture());
    bgfx::setTexture(1U, normalSampler_, desc.gbuffer->NormalTexture());
    bgfx::setTexture(2U, materialSampler_, desc.gbuffer->MaterialTexture());
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::submit(desc.viewId, program_);

    stats += lightingStats;
    ++stats.submittedDrawCallCount;
    return true;
}

bool SceneDeferredLightingPass::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && bgfx::isValid(albedoSampler_) &&
        bgfx::isValid(normalSampler_) && bgfx::isValid(materialSampler_) &&
        bgfx::isValid(lightDirKindUniform_) && bgfx::isValid(lightColorIntensityUniform_) &&
        bgfx::isValid(lightParamsUniform_) && bgfx::isValid(ambientColorUniform_) &&
        bgfx::isValid(environmentZenithUniform_) && bgfx::isValid(environmentGroundUniform_) &&
        bgfx::isValid(environmentParamsUniform_);
}

} // namespace kb::render
