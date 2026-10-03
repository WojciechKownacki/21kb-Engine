#include "kb/render/gi/SceneGiResolvePass.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "kb/render/ShaderLoader.hpp"
#include "renderer/RendererMatrixMath.hpp"
#include "scene/lighting/SceneLightingPacker.hpp"

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
    viewProjUniform_ = bgfx::createUniform("u_giViewProj", bgfx::UniformType::Mat4);
    if (!IsInitialized()) {
        Shutdown();
        return false;
    }
    return true;
}

void SceneGiResolvePass::Shutdown() noexcept {
    giUniforms_.Destroy();
    for (bgfx::UniformHandle* handle : { &normalSampler_, &depthSampler_, &litSampler_, &cameraPositionUniform_,
             &inverseViewProjectionUniform_, &depthParamsUniform_, &viewProjUniform_ }) {
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
        bgfx::isValid(depthParamsUniform_) && bgfx::isValid(viewProjUniform_);
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
    bgfx::setUniform(viewProjUniform_, viewProjection.data());
    const SceneGiBinding binding = desc.history->Binding();
    giUniforms_.Set(&binding, desc.lightingConfig, BGFX_INVALID_HANDLE);
    bgfx::setTexture(kNormalStage, normalSampler_, desc.gbuffer->NormalTexture());
    bgfx::setTexture(kDepthStage, depthSampler_, desc.gbuffer->DepthTexture());
    bgfx::setTexture(kLitStage, litSampler_, desc.history->LitTexture());
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::submit(desc.viewId, program_);
    desc.history->CommitResolve(viewProjection);
    return true;
}

} // namespace kb::render
