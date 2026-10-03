#include "kb/render/gi/SceneGiUniforms.hpp"

#include <array>
#include <initializer_list>

namespace kb::render {
namespace {

constexpr float kGiHitThicknessFactor = 0.5F;
constexpr float kGiDepthTolerance = 0.05F;

} // namespace

void SceneGiUniforms::Create() {
    accumSampler_ = bgfx::createUniform("s_giAccum", bgfx::UniformType::Sampler);
    prevViewProjUniform_ = bgfx::createUniform("u_giPrevViewProj", bgfx::UniformType::Mat4);
    paramsUniform_ = bgfx::createUniform("u_giParams", bgfx::UniformType::Vec4);
    temporalUniform_ = bgfx::createUniform("u_giTemporal", bgfx::UniformType::Vec4);
}

void SceneGiUniforms::Destroy() noexcept {
    for (bgfx::UniformHandle* handle : { &accumSampler_, &prevViewProjUniform_, &paramsUniform_, &temporalUniform_ }) {
        if (bgfx::isValid(*handle)) {
            bgfx::destroy(*handle);
        }
        *handle = BGFX_INVALID_HANDLE;
    }
}

bool SceneGiUniforms::IsValid() const noexcept {
    return bgfx::isValid(accumSampler_) && bgfx::isValid(prevViewProjUniform_) && bgfx::isValid(paramsUniform_) &&
        bgfx::isValid(temporalUniform_);
}

void SceneGiUniforms::Set(const SceneGiBinding* binding, const SceneRenderLightingConfig& config, bgfx::TextureHandle unavailableTexture) const {
    const bool enabled = config.globalIllumination == SceneRenderGlobalIlluminationMode::SsGi && binding != nullptr;
    const bool active = enabled && binding->active && bgfx::isValid(binding->accum);
    constexpr std::array<float, 16> identity{ 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F };
    const std::array<float, 4> params{
        enabled ? config.giIntensity : 0.0F, config.giRange, static_cast<float>(enabled ? binding->frameIndex % 1024U : 0U), kGiHitThicknessFactor };
    const std::array<float, 4> temporal{ config.giHistoryWeight, kGiDepthTolerance, active ? 1.0F : 0.0F, 0.0F };
    bgfx::setUniform(prevViewProjUniform_, active ? binding->accumViewProjection.data() : identity.data());
    bgfx::setUniform(paramsUniform_, params.data());
    bgfx::setUniform(temporalUniform_, temporal.data());
    bgfx::setTexture(kAccumStage, accumSampler_, active ? binding->accum : unavailableTexture);
}

} // namespace kb::render
