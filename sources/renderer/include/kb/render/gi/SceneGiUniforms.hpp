#pragma once

#include "kb/render/gi/SceneGiHistory.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

// The uniforms of ssgi.sh, shared by the resolve pass (writes the accumulated bounce light) and the
// deferred lighting pass (reads it).
class SceneGiUniforms {
public:
    static constexpr std::uint8_t kAccumStage = 8U;

    void Create();
    void Destroy() noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    // `binding` may be null; `unavailableTexture` stands in for the accumulation texture then.
    // The intensity is zero unless screen-space GI is enabled, which switches the shader path off.
    void Set(const SceneGiBinding* binding, const SceneRenderLightingConfig& config, bgfx::TextureHandle unavailableTexture) const;

private:
    bgfx::UniformHandle accumSampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle prevViewProjUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle paramsUniform_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle temporalUniform_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
