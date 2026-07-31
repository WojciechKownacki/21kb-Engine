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

[[nodiscard]] bgfx::TextureHandle CreateFallbackWhiteTexture() {
    const std::uint32_t white = 0xFFFF'FFFFU;
    const bgfx::Memory* memory = bgfx::copy(&white, sizeof(white));
    return bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
}

[[nodiscard]] bgfx::TextureHandle CreateFallbackBlackTexture() {
    constexpr std::uint32_t black = 0x0000'00FFU;
    const bgfx::Memory* memory = bgfx::copy(&black, sizeof(black));
    return bgfx::createTexture2D(1U, 1U, false, 1U, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_NONE, memory);
}

[[nodiscard]] std::array<float, 16> ZeroMatrix() noexcept {
    return std::array<float, 16>{};
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
    static_cast<void>(debugNormalPresentPass_.Initialize());
    albedoSampler_ = bgfx::createUniform("s_gbufferAlbedo", bgfx::UniformType::Sampler);
    normalSampler_ = bgfx::createUniform("s_gbufferNormal", bgfx::UniformType::Sampler);
    materialSampler_ = bgfx::createUniform("s_gbufferMaterial", bgfx::UniformType::Sampler);
    surfaceSampler_ = bgfx::createUniform("s_gbufferSurface", bgfx::UniformType::Sampler);
    depthSampler_ = bgfx::createUniform("s_gbufferDepth", bgfx::UniformType::Sampler);
    lightDirKindUniform_ = bgfx::createUniform("u_deferredLightDirKind", bgfx::UniformType::Vec4, kMaxSceneForwardPlusLights);
    lightPositionRangeUniform_ = bgfx::createUniform("u_deferredLightPositionRange", bgfx::UniformType::Vec4, kMaxSceneForwardPlusLights);
    lightColorIntensityUniform_ = bgfx::createUniform("u_deferredLightColorIntensity", bgfx::UniformType::Vec4, kMaxSceneForwardPlusLights);
    lightSpotUniform_ = bgfx::createUniform("u_deferredLightSpot", bgfx::UniformType::Vec4, kMaxSceneForwardPlusLights);
    lightParamsUniform_ = bgfx::createUniform("u_deferredLightParams", bgfx::UniformType::Vec4);
    ambientColorUniform_ = bgfx::createUniform("u_deferredAmbientColor", bgfx::UniformType::Vec4);
    environmentZenithUniform_ = bgfx::createUniform("u_deferredEnvironmentZenith", bgfx::UniformType::Vec4);
    environmentGroundUniform_ = bgfx::createUniform("u_deferredEnvironmentGround", bgfx::UniformType::Vec4);
    environmentParamsUniform_ = bgfx::createUniform("u_deferredEnvironmentParams", bgfx::UniformType::Vec4);
    cameraPositionUniform_ = bgfx::createUniform("u_deferredCameraPosition", bgfx::UniformType::Vec4);
    inverseViewProjectionUniform_ = bgfx::createUniform("u_deferredInverseViewProjection", bgfx::UniformType::Mat4);
    depthParamsUniform_ = bgfx::createUniform("u_deferredDepthParams", bgfx::UniformType::Vec4);
    shadowMapSampler_ = bgfx::createUniform("s_deferredShadowMap", bgfx::UniformType::Sampler);
    shadowViewProjUniform_ = bgfx::createUniform("u_deferredShadowViewProj", bgfx::UniformType::Mat4);
    shadowParamsUniform_ = bgfx::createUniform("u_deferredShadowParams", bgfx::UniformType::Vec4);
    backdropHorizonUniform_ = bgfx::createUniform("u_deferredBackdropHorizon", bgfx::UniformType::Vec4);
    backdropZenithUniform_ = bgfx::createUniform("u_deferredBackdropZenith", bgfx::UniformType::Vec4);
    backdropParamsUniform_ = bgfx::createUniform("u_deferredBackdropParams", bgfx::UniformType::Vec4);
    backdropEnvironmentSampler_ = bgfx::createUniform("s_deferredBackdropEnvironment", bgfx::UniformType::Sampler);
    fallbackShadowTexture_ = CreateFallbackWhiteTexture();
    fallbackBackdropEnvironmentTexture_ = CreateFallbackBlackTexture();
    {
        std::ostringstream message;
        message << "Initialize handles program=" << HandleValue(program_)
                << " debugNormalPresent=" << (debugNormalPresentPass_.IsInitialized() ? "true" : "false")
                << " albedoSampler=" << HandleValue(albedoSampler_)
                << " normalSampler=" << HandleValue(normalSampler_)
                << " materialSampler=" << HandleValue(materialSampler_)
                << " surfaceSampler=" << HandleValue(surfaceSampler_)
                << " depthSampler=" << HandleValue(depthSampler_)
                << " renderer=" << static_cast<int>(bgfx::getRendererType());
        WriteRendererDebugLog("deferred_lighting", message.str());
        WriteRendererMaterialGraphDebugLog("deferred", message.str());
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
                << " surfaceSampler=" << HandleValue(surfaceSampler_)
                << " depthSampler=" << HandleValue(depthSampler_);
        WriteRendererDebugLog("deferred_lighting", message.str());
    }
    if (bgfx::isValid(backdropParamsUniform_)) {
        bgfx::destroy(backdropParamsUniform_);
        backdropParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(backdropEnvironmentSampler_)) {
        bgfx::destroy(backdropEnvironmentSampler_);
        backdropEnvironmentSampler_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(backdropZenithUniform_)) {
        bgfx::destroy(backdropZenithUniform_);
        backdropZenithUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(backdropHorizonUniform_)) {
        bgfx::destroy(backdropHorizonUniform_);
        backdropHorizonUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackShadowTexture_)) {
        bgfx::destroy(fallbackShadowTexture_);
        fallbackShadowTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(fallbackBackdropEnvironmentTexture_)) {
        bgfx::destroy(fallbackBackdropEnvironmentTexture_);
        fallbackBackdropEnvironmentTexture_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowParamsUniform_)) {
        bgfx::destroy(shadowParamsUniform_);
        shadowParamsUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowViewProjUniform_)) {
        bgfx::destroy(shadowViewProjUniform_);
        shadowViewProjUniform_ = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(shadowMapSampler_)) {
        bgfx::destroy(shadowMapSampler_);
        shadowMapSampler_ = BGFX_INVALID_HANDLE;
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
    if (bgfx::isValid(surfaceSampler_)) {
        bgfx::destroy(surfaceSampler_);
        surfaceSampler_ = BGFX_INVALID_HANDLE;
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
    debugNormalPresentPass_.Shutdown();
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
        WriteRendererMaterialGraphDebugLog("deferred", message.str());
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
                << " surfaceTex=" << HandleValue(desc.gbuffer->SurfaceTexture())
                << " depthTex=" << HandleValue(desc.gbuffer->DepthTexture())
                << " program=" << HandleValue(program_)
                << " clear=0x" << std::hex << desc.clearRgba << std::dec
                << " lightsPath=" << static_cast<int>(desc.lightingConfig.lightingPath)
                << " envMode=" << static_cast<int>(desc.lightingConfig.environmentMode);
        WriteRendererDebugLog("deferred_lighting", message.str());
        WriteRendererMaterialGraphDebugLog("deferred", message.str());
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

    if (desc.lightingConfig.debugView == SceneRenderDebugView::GBufferNormal) {
        FullscreenTextureOutputTransform debugTransform{};
        debugTransform.gamma = 1.0F;
        debugTransform.tonemap = FullscreenTextureTonemapOperator::None;
        debugTransform.colorGradingLutStrength = 0.0F;
        const bool submitted = debugNormalPresentPass_.Submit(FullscreenTexturePassDesc{
            .viewId = desc.viewId,
            .sourceTexture = desc.gbuffer->NormalTexture(),
            .frameBuffer = desc.frameBuffer,
            .extent = desc.extent,
            .outputTransform = debugTransform,
            .clearRgba = desc.clearRgba,
            .clearTarget = true,
            .viewName = "KB GBuffer Normal Debug",
        });
        std::ostringstream message;
        message << "GBufferNormal debug submit"
                << " submitted=" << (submitted ? "true" : "false")
                << " normalTex=" << HandleValue(desc.gbuffer->NormalTexture())
                << " programReady=" << (debugNormalPresentPass_.IsInitialized() ? "true" : "false");
        WriteRendererDebugLog("deferred_lighting", message.str());
        WriteRendererMaterialGraphDebugLog("deferred", message.str());
        if (!submitted) {
            return false;
        }
        ++stats.submittedDrawCallCount;
        return true;
    }

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
        WriteRendererMaterialGraphDebugLog("deferred", message.str());
    }
    bgfx::setUniform(lightDirKindUniform_, lighting.dirKind.data(), kMaxSceneForwardPlusLights);
    bgfx::setUniform(lightPositionRangeUniform_, lighting.positionRange.data(), kMaxSceneForwardPlusLights);
    bgfx::setUniform(lightColorIntensityUniform_, lighting.colorIntensity.data(), kMaxSceneForwardPlusLights);
    bgfx::setUniform(lightSpotUniform_, lighting.spot.data(), kMaxSceneForwardPlusLights);
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
    const bool gradientBackdrop = desc.worldBackdrop != nullptr &&
        (desc.worldBackdrop->mode == SceneRenderWorldBackdropMode::VerticalGradient ||
         desc.worldBackdrop->mode == SceneRenderWorldBackdropMode::ProceduralSky);
    const bool environmentBackdrop = desc.worldBackdrop != nullptr &&
        desc.worldBackdrop->mode == SceneRenderWorldBackdropMode::EnvironmentMap &&
        bgfx::isValid(desc.worldBackdropEnvironment);
    const std::array<float, 4> backdropHorizon = gradientBackdrop
        ? std::array<float, 4>{ desc.worldBackdrop->horizonColor[0], desc.worldBackdrop->horizonColor[1], desc.worldBackdrop->horizonColor[2], 1.0F }
        : std::array<float, 4>{};
    const std::array<float, 4> backdropZenith = gradientBackdrop
        ? std::array<float, 4>{ desc.worldBackdrop->zenithColor[0], desc.worldBackdrop->zenithColor[1], desc.worldBackdrop->zenithColor[2], 1.0F }
        : std::array<float, 4>{};
    const std::array<float, 4> backdropParams = gradientBackdrop
        ? std::array<float, 4>{ 1.0F, desc.worldBackdrop->horizonHeight, desc.worldBackdrop->gradientExponent,
              desc.worldBackdrop->mode == SceneRenderWorldBackdropMode::ProceduralSky ? 1.0F : 0.0F }
        : environmentBackdrop ? std::array<float, 4>{ 2.0F, 0.0F, 0.0F, 0.0F } : std::array<float, 4>{};
    bgfx::setUniform(cameraPositionUniform_, cameraPosition.data());
    bgfx::setUniform(inverseViewProjectionUniform_, inverseViewProjection.data());
    bgfx::setUniform(depthParamsUniform_, depthParams.data());
    bgfx::setUniform(backdropHorizonUniform_, backdropHorizon.data());
    bgfx::setUniform(backdropZenithUniform_, backdropZenith.data());
    bgfx::setUniform(backdropParamsUniform_, backdropParams.data());
    // Deferred's key light used to be applied at full strength everywhere, with no shadow term at
    // all, while the forward opaque pass darkens light index 0 by a sampled shadow map -- same light,
    // same scene, but deferred came out visibly brighter/flatter. Feed the same shadow binding used
    // by the forward/G-buffer passes into the lighting resolve so both paths agree.
    const bool shadowValid = desc.shadowMap != nullptr && desc.shadowMap->IsValid();
    static const std::array<float, 16> disabledShadowViewProj = ZeroMatrix();
    static const std::array<float, 4> disabledShadowParams{};
    bgfx::setUniform(shadowViewProjUniform_, shadowValid ? desc.shadowMap->lightViewProjection.data() : disabledShadowViewProj.data());
    bgfx::setUniform(shadowParamsUniform_, shadowValid ? desc.shadowMap->params.data() : disabledShadowParams.data());
    {
        std::ostringstream message;
        message << "bind-gbuffer viewId=" << desc.viewId
                << " albedoTex=" << HandleValue(desc.gbuffer->AlbedoTexture())
                << " normalTex=" << HandleValue(desc.gbuffer->NormalTexture())
                << " materialTex=" << HandleValue(desc.gbuffer->MaterialTexture())
                << " surfaceTex=" << HandleValue(desc.gbuffer->SurfaceTexture())
                << " depthTex=" << HandleValue(desc.gbuffer->DepthTexture())
                << " shadowValid=" << (shadowValid ? "true" : "false")
                << " shadowTex=" << HandleValue(shadowValid ? desc.shadowMap->depthTexture : fallbackShadowTexture_)
                << " backdropMode=" << (desc.worldBackdrop != nullptr ? static_cast<int>(desc.worldBackdrop->mode) : -1)
                << " gradientBackdrop=" << (gradientBackdrop ? "true" : "false")
                << " environmentBackdrop=" << (environmentBackdrop ? "true" : "false")
                << " backdropParams=(" << backdropParams[0] << ',' << backdropParams[1] << ',' << backdropParams[2] << ',' << backdropParams[3] << ')'
                << " camera=(" << cameraPosition[0] << ',' << cameraPosition[1] << ',' << cameraPosition[2] << ',' << cameraPosition[3] << ')'
                << " depthHomogeneous=" << (SceneDepthPolicy::HomogeneousDepth() ? "true" : "false");
        WriteRendererMaterialGraphDebugLog("deferred", message.str());
    }
    bgfx::setTexture(0U, albedoSampler_, desc.gbuffer->AlbedoTexture());
    bgfx::setTexture(1U, normalSampler_, desc.gbuffer->NormalTexture());
    bgfx::setTexture(2U, materialSampler_, desc.gbuffer->MaterialTexture());
    bgfx::setTexture(3U, surfaceSampler_, desc.gbuffer->SurfaceTexture());
    bgfx::setTexture(4U, depthSampler_, desc.gbuffer->DepthTexture());
    bgfx::setTexture(5U, shadowMapSampler_, shadowValid ? desc.shadowMap->depthTexture : fallbackShadowTexture_);
    bgfx::setTexture(6U, backdropEnvironmentSampler_, environmentBackdrop ? desc.worldBackdropEnvironment : fallbackBackdropEnvironmentTexture_);
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
        WriteRendererMaterialGraphDebugLog("deferred", message.str());
    }
    return true;
}

bool SceneDeferredLightingPass::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && debugNormalPresentPass_.IsInitialized() && bgfx::isValid(albedoSampler_) &&
        bgfx::isValid(normalSampler_) && bgfx::isValid(materialSampler_) && bgfx::isValid(surfaceSampler_) &&
        bgfx::isValid(depthSampler_) &&
        bgfx::isValid(lightDirKindUniform_) && bgfx::isValid(lightPositionRangeUniform_) &&
        bgfx::isValid(lightColorIntensityUniform_) && bgfx::isValid(lightSpotUniform_) &&
        bgfx::isValid(lightParamsUniform_) && bgfx::isValid(ambientColorUniform_) &&
        bgfx::isValid(environmentZenithUniform_) && bgfx::isValid(environmentGroundUniform_) &&
        bgfx::isValid(environmentParamsUniform_) && bgfx::isValid(cameraPositionUniform_) &&
        bgfx::isValid(inverseViewProjectionUniform_) && bgfx::isValid(depthParamsUniform_) &&
        bgfx::isValid(shadowMapSampler_) && bgfx::isValid(shadowViewProjUniform_) && bgfx::isValid(shadowParamsUniform_) &&
        bgfx::isValid(backdropHorizonUniform_) && bgfx::isValid(backdropZenithUniform_) && bgfx::isValid(backdropParamsUniform_) &&
        bgfx::isValid(backdropEnvironmentSampler_) && bgfx::isValid(fallbackShadowTexture_) &&
        bgfx::isValid(fallbackBackdropEnvironmentTexture_);
}

} // namespace kb::render
