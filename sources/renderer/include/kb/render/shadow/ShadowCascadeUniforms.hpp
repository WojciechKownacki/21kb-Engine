#pragma once

#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

// Uniforms read by shadow_cascades.sh. bgfx hands out one handle per uniform name, so the
// forward and deferred lighting passes each create/destroy their own reference.
struct ShadowCascadeUniforms {
    bgfx::UniformHandle viewProjection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle atlas = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle info = BGFX_INVALID_HANDLE;

    void Create() {
        constexpr auto cascades = static_cast<std::uint16_t>(SceneRenderShadowMapBinding::kMaxCascades);
        viewProjection = bgfx::createUniform("u_shadowCascadeViewProj", bgfx::UniformType::Mat4, cascades);
        atlas = bgfx::createUniform("u_shadowCascadeAtlas", bgfx::UniformType::Vec4, cascades);
        info = bgfx::createUniform("u_shadowCascadeInfo", bgfx::UniformType::Vec4);
    }

    void Destroy() noexcept {
        for (bgfx::UniformHandle* handle : { &viewProjection, &atlas, &info }) {
            if (bgfx::isValid(*handle)) {
                bgfx::destroy(*handle);
            }
            *handle = BGFX_INVALID_HANDLE;
        }
    }

    [[nodiscard]] bool IsValid() const noexcept {
        return bgfx::isValid(viewProjection) && bgfx::isValid(atlas) && bgfx::isValid(info);
    }

    // A null or invalid binding publishes zero cascades, which the shader treats as "no shadow".
    void Set(const SceneRenderShadowMapBinding* binding) const {
        static const SceneRenderShadowMapBinding disabled{ .cascadeCount = 0U, .cascadeInfo = {} };
        const SceneRenderShadowMapBinding& source = binding != nullptr && binding->IsValid() ? *binding : disabled;
        bgfx::setUniform(viewProjection, source.cascadeViewProjection.data(), SceneRenderShadowMapBinding::kMaxCascades);
        bgfx::setUniform(atlas, source.cascadeAtlas.data(), SceneRenderShadowMapBinding::kMaxCascades);
        bgfx::setUniform(info, source.cascadeInfo.data());
    }
};

} // namespace kb::render
