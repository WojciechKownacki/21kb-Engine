#include "kb/render/SceneDeferredLightingPass.hpp"

#include "kb/render/ShaderLoader.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "renderer/RendererMatrixMath.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"
#include "renderer/RendererDebugLog.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>

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

[[nodiscard]] std::uint16_t HandleValue(bgfx::ProgramHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::UniformHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::TextureHandle handle) noexcept {
    return handle.idx;
}

[[nodiscard]] std::uint16_t HandleValue(bgfx::FrameBufferHandle handle) noexcept {
    return handle.idx;
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
        WriteRendererDebugLog("deferred_lighting", "Initialize skipped already initialized");
        return true;
    }

    WriteRendererDebugLog("deferred_lighting", "Initialize begin");
    program_ = ShaderLoader::LoadProgram("vs_present.sc", "fs_deferred_lighting.sc");
    albedoSampler_ = bgfx::createUniform("s_gbufferAlbedo", bgfx::UniformType::Sampler);
    normalSampler_ = bgfx::createUniform("s_gbufferNormal", bgfx::UniformType::Sampler);
    materialSampler_ = bgfx::createUniform("s_gbufferMaterial", bgfx::UniformType::Sampler);
    depthSampler_ = bgfx::createUniform("s_gbufferDepth", bgfx::UniformType::Sampler);
    lightDirKindUniform_ = bgfx::createUniform("u_deferredLightDirKind", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightPositionRangeUniform_ = bgfx::createUniform("u_deferredLightPositionRange", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightColorIntensityUniform_ = bgfx::createUniform("u_deferredLightColorIntensity", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightSpotUniform_ = bgfx::createUniform("u_deferredLightSpot", bgfx::UniformType::Vec4, kMaxSceneForwardLights);
    lightParamsUniform_ = bgfx::createUniform("u_deferredLightParams", bgfx::UniformType::Vec4);
    ambientColorUniform_ = bgfx::createUniform("u_deferredAmbientColor", bgfx::UniformType::Vec4);
    environmentZenithUniform_ = bgfx::createUniform("u_deferredEnvironmentZenith", bgfx::UniformType::Vec4);
    environmentGroundUniform_ = bgfx::createUniform("u_deferredEnvironmentGround", bgfx::UniformType::Vec4);
    environmentParamsUniform_ = bgfx::createUniform("u_deferredEnvironmentParams", bgfx::UniformType::Vec4);
    cameraPositionUniform_ = bgfx::createUniform("u_deferredCameraPosition", bgfx::UniformType::Vec4);
    inverseViewProjectionUniform_ = bgfx::createUniform("u_deferredInverseViewProjection", bgfx::UniformType::Mat4);
    depthParamsUniform_ = bgfx::createUniform("u_deferredDepthParams", bgfx::UniformType::Vec4);
    {
        std::ostringstream message;
        message << "Initialize handles program=" << HandleValue(program_)
                << " albedoSampler=" << HandleValue(albedoSampler_)
                << " normalSampler=" << HandleValue(normalSampler_)
                << " materialSampler=" << HandleValue(materialSampler_)
                << " depthSampler=" << HandleValue(depthSampler_)
                << " renderer=" << static_cast<int>(bgfx::getRendererType());
        WriteRendererDebugLog("deferred_lighting", message.str());
    }
    if (!IsInitialized()) {
        WriteRendererDebugLog("deferred_lighting", "Initialize failed; shutting down");
        Shutdown();
        return false;
    }
    WriteRendererDebugLog("deferred_lighting", "Initialize end ok");
    return true;
}

void SceneDeferredLightingPass::Shutdown() noexcept {
    if (IsInitialized() || bgfx::isValid(program_)) {
        std::ostringstream message;
        message << "Shutdown program=" << HandleValue(program_)
                << " albedoSampler=" << HandleValue(albedoSampler_)
                << " normalSampler=" << HandleValue(normalSampler_)
                << " materialSampler=" << HandleValue(materialSampler_)
                << " depthSampler=" << HandleValue(depthSampler_);
        WriteRendererDebugLog("deferred_lighting", message.str());
    }
    if (bgfx::isValid(depthParamsUniform_)) {
        bgfx::destroy(depthParamsUniform_);
        depthParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(inverseViewProjectionUniform_)) {
        bgfx::destroy(inverseViewProjectionUniform_);
        inverseViewProjectionUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(cameraPositionUniform_)) {
        bgfx::destroy(cameraPositionUniform_);
        cameraPositionUniform_ = BGFX_INVALID_HANDLE;
    }
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
    if (bgfx::isValid(lightSpotUniform_)) {
        bgfx::destroy(lightSpotUniform_);
        lightSpotUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightColorIntensityUniform_)) {
        bgfx::destroy(lightColorIntensityUniform_);
        lightColorIntensityUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightPositionRangeUniform_)) {
        bgfx::destroy(lightPositionRangeUniform_);
        lightPositionRangeUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(lightDirKindUniform_)) {
        bgfx::destroy(lightDirKindUniform_);
        lightDirKindUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(materialSampler_)) {
        bgfx::destroy(materialSampler_);
        materialSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(depthSampler_)) {
        bgfx::destroy(depthSampler_);
        depthSampler_ = BGFX_INVALID_HANDLE;
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
        std::ostringstream message;
        message << "Submit invalid initialized=" << (IsInitialized() ? "true" : "false")
                << " descValid=" << (desc.IsValid() ? "true" : "false")
                << " gbuffer=" << (desc.gbuffer != nullptr ? "true" : "false")
                << " renderScene=" << (desc.renderScene != nullptr ? "true" : "false")
                << " extent=" << desc.extent.width << 'x' << desc.extent.height;
        WriteRendererDebugLog("deferred_lighting", message.str());
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
        std::ostringstream message;
        message << "Submit failed transient vertices requested=" << vertexCount
                << " available=" << bgfx::getAvailTransientVertexBuffer(vertexCount, layout);
        WriteRendererDebugLog("deferred_lighting", message.str());
        return false;
    }

    {
        std::ostringstream message;
        message << "Submit begin viewId=" << desc.viewId
                << " fb=" << HandleValue(desc.frameBuffer)
                << " extent=" << desc.extent.width << 'x' << desc.extent.height
                << " gbufferFb=" << HandleValue(desc.gbuffer->FrameBuffer())
                << " albedoTex=" << HandleValue(desc.gbuffer->AlbedoTexture())
                << " normalTex=" << HandleValue(desc.gbuffer->NormalTexture())
                << " materialTex=" << HandleValue(desc.gbuffer->MaterialTexture())
                << " depthTex=" << HandleValue(desc.gbuffer->DepthTexture())
                << " program=" << HandleValue(program_)
                << " clear=0x" << std::hex << desc.clearRgba << std::dec
                << " lightsPath=" << static_cast<int>(desc.lightingConfig.lightingPath)
                << " envMode=" << static_cast<int>(desc.lightingConfig.environmentMode);
        WriteRendererDebugLog("deferred_lighting", message.str());
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
    {
        std::ostringstream message;
        message << "Lighting packed submittedForward=" << lightingStats.submittedForwardLightCount
                << " skippedForward=" << lightingStats.skippedForwardLightCount
                << " invalidLights=" << lightingStats.invalidLightCount
                << " envSubmitted=" << lightingStats.submittedEnvironmentLightingCount
                << " lightParams=(" << lighting.params[0] << ',' << lighting.params[1] << ',' << lighting.params[2] << ',' << lighting.params[3] << ')'
                << " ambient=(" << lighting.ambient[0] << ',' << lighting.ambient[1] << ',' << lighting.ambient[2] << ',' << lighting.ambient[3] << ')';
        WriteRendererDebugLog("deferred_lighting", message.str());
    }
    bgfx::setUniform(lightDirKindUniform_, lighting.dirKind.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightPositionRangeUniform_, lighting.positionRange.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightColorIntensityUniform_, lighting.colorIntensity.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightSpotUniform_, lighting.spot.data(), kMaxSceneForwardLights);
    bgfx::setUniform(lightParamsUniform_, lighting.params.data());
    bgfx::setUniform(ambientColorUniform_, lighting.ambient.data());
    bgfx::setUniform(environmentZenithUniform_, lighting.environmentZenith.data());
    bgfx::setUniform(environmentGroundUniform_, lighting.environmentGround.data());
    bgfx::setUniform(environmentParamsUniform_, lighting.environmentParams.data());
    const std::array<float, 16> viewProjection = desc.camera != nullptr
        ? RendererMatrixMath::ViewProjection(*desc.camera)
        : RendererMatrixMath::Identity();
    const std::array<float, 16> inverseViewProjection = RendererMatrixMath::Inverse(viewProjection);
    const std::array<float, 4> cameraPosition = SceneLightingPacker::CameraPosition(desc.camera);
    const std::array<float, 4> depthParams{ SceneDepthPolicy::HomogeneousDepth() ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F };
    bgfx::setUniform(cameraPositionUniform_, cameraPosition.data());
    bgfx::setUniform(inverseViewProjectionUniform_, inverseViewProjection.data());
    bgfx::setUniform(depthParamsUniform_, depthParams.data());
    bgfx::setTexture(0U, albedoSampler_, desc.gbuffer->AlbedoTexture());
    bgfx::setTexture(1U, normalSampler_, desc.gbuffer->NormalTexture());
    bgfx::setTexture(2U, materialSampler_, desc.gbuffer->MaterialTexture());
    bgfx::setTexture(3U, depthSampler_, desc.gbuffer->DepthTexture());
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::submit(desc.viewId, program_);

    stats += lightingStats;
    ++stats.submittedDrawCallCount;
    {
        std::ostringstream message;
        message << "Submit end ok drawCalls=" << stats.submittedDrawCallCount
                << " state=0x" << std::hex << (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A) << std::dec;
        WriteRendererDebugLog("deferred_lighting", message.str());
    }
    return true;
}

bool SceneDeferredLightingPass::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && bgfx::isValid(albedoSampler_) &&
        bgfx::isValid(normalSampler_) && bgfx::isValid(materialSampler_) && bgfx::isValid(depthSampler_) &&
        bgfx::isValid(lightDirKindUniform_) && bgfx::isValid(lightPositionRangeUniform_) &&
        bgfx::isValid(lightColorIntensityUniform_) && bgfx::isValid(lightSpotUniform_) &&
        bgfx::isValid(lightParamsUniform_) && bgfx::isValid(ambientColorUniform_) &&
        bgfx::isValid(environmentZenithUniform_) && bgfx::isValid(environmentGroundUniform_) &&
        bgfx::isValid(environmentParamsUniform_) && bgfx::isValid(cameraPositionUniform_) &&
        bgfx::isValid(inverseViewProjectionUniform_) && bgfx::isValid(depthParamsUniform_);
}

} // namespace kb::render
