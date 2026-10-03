#include "kb/render/gi/SceneGiUniforms.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace kb::render {
namespace {

constexpr float kGiHitThicknessFactor = 0.5F;
constexpr float kGiDepthTolerance = 0.05F;

} // namespace

void SceneGiUniforms::Create() {
    accumSampler_ = bgfx::createUniform("s_giAccum", bgfx::UniformType::Sampler);
    aoSampler_ = bgfx::createUniform("s_aoAccum", bgfx::UniformType::Sampler);
    litSampler_ = bgfx::createUniform("s_ssrLit", bgfx::UniformType::Sampler);
    viewProjUniform_ = bgfx::createUniform("u_giViewProj", bgfx::UniformType::Mat4);
    prevViewProjUniform_ = bgfx::createUniform("u_giPrevViewProj", bgfx::UniformType::Mat4);
    paramsUniform_ = bgfx::createUniform("u_giParams", bgfx::UniformType::Vec4);
    temporalUniform_ = bgfx::createUniform("u_giTemporal", bgfx::UniformType::Vec4);
    aoParamsUniform_ = bgfx::createUniform("u_aoParams", bgfx::UniformType::Vec4);
    ssrParamsUniform_ = bgfx::createUniform("u_ssrParams", bgfx::UniformType::Vec4);
}

void SceneGiUniforms::Destroy() noexcept {
    for (bgfx::UniformHandle* handle : { &accumSampler_, &aoSampler_, &litSampler_, &viewProjUniform_, &prevViewProjUniform_,
             &paramsUniform_, &temporalUniform_, &aoParamsUniform_, &ssrParamsUniform_ }) {
        if (bgfx::isValid(*handle)) {
            bgfx::destroy(*handle);
        }
        *handle = BGFX_INVALID_HANDLE;
    }
}

bool SceneGiUniforms::IsValid() const noexcept {
    return bgfx::isValid(accumSampler_) && bgfx::isValid(aoSampler_) && bgfx::isValid(litSampler_) &&
        bgfx::isValid(viewProjUniform_) && bgfx::isValid(prevViewProjUniform_) && bgfx::isValid(paramsUniform_) &&
        bgfx::isValid(temporalUniform_) && bgfx::isValid(aoParamsUniform_) && bgfx::isValid(ssrParamsUniform_);
}

void SceneGiUniforms::Set(const SceneGiBinding* binding, const SceneRenderLightingConfig& config,
    const std::array<float, 16>& viewProjection, bgfx::TextureHandle unavailableTexture) const {
    const bool present = binding != nullptr;
    const bool giEnabled = present && config.globalIllumination == SceneRenderGlobalIlluminationMode::SsGi;
    const bool aoEnabled = present && config.ambientOcclusionEnabled;
    const bool ssrEnabled = present && config.screenSpaceReflectionsEnabled;
    const bool active = present && binding->active && bgfx::isValid(binding->accum);
    const std::array<float, 4> params{
        giEnabled ? config.giIntensity : 0.0F, config.giRange, static_cast<float>(present ? binding->frameIndex % 1024U : 0U),
        kGiHitThicknessFactor };
    const std::array<float, 4> temporal{ config.giHistoryWeight, kGiDepthTolerance, active ? 1.0F : 0.0F, 0.0F };
    const std::array<float, 4> aoParams{ aoEnabled ? std::max(config.aoIntensity, 0.0F) : 0.0F, std::max(config.aoRadius, 0.01F), 0.0F, 0.0F };
    const std::array<float, 4> ssrParams{
        ssrEnabled ? std::clamp(config.ssrIntensity, 0.0F, 1.0F) : 0.0F, std::max(config.ssrMaxDistance, 0.1F), 0.0F, kGiHitThicknessFactor };
    bgfx::setUniform(viewProjUniform_, viewProjection.data());
    bgfx::setUniform(prevViewProjUniform_, active ? binding->accumViewProjection.data() : viewProjection.data());
    bgfx::setUniform(paramsUniform_, params.data());
    bgfx::setUniform(temporalUniform_, temporal.data());
    bgfx::setUniform(aoParamsUniform_, aoParams.data());
    bgfx::setUniform(ssrParamsUniform_, ssrParams.data());
    bgfx::setTexture(kAccumStage, accumSampler_, active ? binding->accum : unavailableTexture);
    bgfx::setTexture(kAoStage, aoSampler_, active && bgfx::isValid(binding->aoAccum) ? binding->aoAccum : unavailableTexture);
    bgfx::setTexture(kLitStage, litSampler_, active && bgfx::isValid(binding->lit) ? binding->lit : unavailableTexture);
}

} // namespace kb::render
