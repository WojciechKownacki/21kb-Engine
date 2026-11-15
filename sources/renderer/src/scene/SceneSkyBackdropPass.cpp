#include "kb/render/scene/SceneSkyBackdropPass.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/ShaderLoader.hpp"
#include "renderer/RendererDebugLog.hpp"
#include "renderer/RendererMatrixMath.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace kb::render {
namespace {

struct BackdropVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
};

[[nodiscard]] bgfx::VertexLayout BackdropVertexLayout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
    return layout;
}

template <typename Handle>
void DestroyHandle(Handle& handle) noexcept {
    if (bgfx::isValid(handle)) {
        bgfx::destroy(handle);
        handle = BGFX_INVALID_HANDLE;
    }
}

} // namespace

SceneSkyBackdropPass::~SceneSkyBackdropPass() {
    Shutdown();
}

bool SceneSkyBackdropPass::Initialize() {
    if (IsInitialized()) {
        return true;
    }
    program_ = ShaderLoader::LoadProgram("vs_sky_backdrop.sc", "fs_sky_backdrop.sc");
    cameraPositionUniform_ = bgfx::createUniform("u_deferredCameraPosition", bgfx::UniformType::Vec4);
    inverseViewProjectionUniform_ = bgfx::createUniform("u_deferredInverseViewProjection", bgfx::UniformType::Mat4);
    depthParamsUniform_ = bgfx::createUniform("u_deferredDepthParams", bgfx::UniformType::Vec4);
    horizonUniform_ = bgfx::createUniform("u_deferredBackdropHorizon", bgfx::UniformType::Vec4);
    zenithUniform_ = bgfx::createUniform("u_deferredBackdropZenith", bgfx::UniformType::Vec4);
    paramsUniform_ = bgfx::createUniform("u_deferredBackdropParams", bgfx::UniformType::Vec4);
    sunUniform_ = bgfx::createUniform("u_deferredBackdropSun", bgfx::UniformType::Vec4);
    sunColorUniform_ = bgfx::createUniform("u_deferredBackdropSunColor", bgfx::UniformType::Vec4);
    environmentSampler_ = bgfx::createUniform("s_deferredBackdropEnvironment", bgfx::UniformType::Sampler);
    const std::array<std::uint8_t, 4> black{ 0U, 0U, 0U, 255U };
    fallbackEnvironment_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0U,
        bgfx::copy(black.data(), static_cast<std::uint32_t>(black.size())));
    if (!IsInitialized()) {
        WriteRendererDebugLog("sky_backdrop", "Forward sky backdrop unavailable: vs_sky_backdrop.sc / fs_sky_backdrop.sc "
            "have no variant for this backend or a handle could not be created");
        Shutdown();
        return false;
    }
    return true;
}

void SceneSkyBackdropPass::Shutdown() noexcept {
    DestroyHandle(fallbackEnvironment_);
    DestroyHandle(environmentSampler_);
    DestroyHandle(sunColorUniform_);
    DestroyHandle(sunUniform_);
    DestroyHandle(paramsUniform_);
    DestroyHandle(zenithUniform_);
    DestroyHandle(horizonUniform_);
    DestroyHandle(depthParamsUniform_);
    DestroyHandle(inverseViewProjectionUniform_);
    DestroyHandle(cameraPositionUniform_);
    DestroyHandle(program_);
}

bool SceneSkyBackdropPass::IsInitialized() const noexcept {
    return bgfx::isValid(program_) && bgfx::isValid(cameraPositionUniform_) &&
        bgfx::isValid(inverseViewProjectionUniform_) && bgfx::isValid(depthParamsUniform_) &&
        bgfx::isValid(horizonUniform_) && bgfx::isValid(zenithUniform_) && bgfx::isValid(paramsUniform_) &&
        bgfx::isValid(sunUniform_) && bgfx::isValid(sunColorUniform_) && bgfx::isValid(environmentSampler_) &&
        bgfx::isValid(fallbackEnvironment_);
}

bool SceneSkyBackdropPass::Submit(bgfx::ViewId viewId, const SceneBackdropUniformValues& backdrop,
    bgfx::TextureHandle environment, const SceneRenderCamera* camera) {
    if (!backdrop.HasSky() || camera == nullptr || !IsInitialized()) {
        return false;
    }
    const bgfx::VertexLayout layout = BackdropVertexLayout();
    if (bgfx::getAvailTransientVertexBuffer(3, layout) < 3U) {
        WriteRendererDebugLog("sky_backdrop", "Forward sky backdrop skipped: no transient vertices left this frame");
        return false;
    }
    // The far plane in clip space: the scene depth test passes only where the depth is still cleared.
    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();
    const float farClip = homogeneousDepth ? SceneDepthPolicy::FarDepth() * 2.0F - 1.0F : SceneDepthPolicy::FarDepth();
    // The fullscreen triangle of the deferred lighting: uv 0,0 at the top-left corner of the target.
    const std::array<BackdropVertex, 3> triangle{
        BackdropVertex{ -1.0F, 1.0F, farClip, 0.0F, 0.0F },
        BackdropVertex{ 3.0F, 1.0F, farClip, 2.0F, 0.0F },
        BackdropVertex{ -1.0F, -3.0F, farClip, 0.0F, 2.0F },
    };
    bgfx::TransientVertexBuffer vertices{};
    bgfx::allocTransientVertexBuffer(&vertices, 3, layout);
    std::memcpy(vertices.data, triangle.data(), sizeof(BackdropVertex) * triangle.size());

    const std::array<float, 16> inverseViewProjection =
        RendererMatrixMath::Inverse(RendererMatrixMath::ViewProjection(*camera));
    const std::array<float, 4> cameraPosition = SceneLightingPacker::CameraPosition(camera);
    const std::array<float, 4> depthParams{ homogeneousDepth ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F };
    bgfx::setUniform(cameraPositionUniform_, cameraPosition.data());
    bgfx::setUniform(inverseViewProjectionUniform_, inverseViewProjection.data());
    bgfx::setUniform(depthParamsUniform_, depthParams.data());
    bgfx::setUniform(horizonUniform_, backdrop.horizon.data());
    bgfx::setUniform(zenithUniform_, backdrop.zenith.data());
    bgfx::setUniform(paramsUniform_, backdrop.params.data());
    bgfx::setUniform(sunUniform_, backdrop.sun.data());
    bgfx::setUniform(sunColorUniform_, backdrop.sunColor.data());
    bgfx::setTexture(0U, environmentSampler_, bgfx::isValid(environment) ? environment : fallbackEnvironment_);
    bgfx::setState(SceneDepthPolicy::SceneDepthReadState());
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::submit(viewId, program_);
    return true;
}

} // namespace kb::render
