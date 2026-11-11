#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/render/StaticLightmapBaker.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <memory>

namespace kb::render {

// The GPU form of a baked light probe grid (light_probes.sh): one RGBA16F 2D texture with the grid's 7
// coefficient slots side by side and its z layers stacked down. A 2D texture can take the stage of another 2D
// texture, which the deferred lighting shader needs: it has no free sampler stage.
class SceneLightProbeTexture {
public:
    SceneLightProbeTexture() = default;
    ~SceneLightProbeTexture();

    SceneLightProbeTexture(const SceneLightProbeTexture&) = delete;
    SceneLightProbeTexture& operator=(const SceneLightProbeTexture&) = delete;

    static constexpr std::uint32_t kSlots = 7U;

    // Uploads `grid` when it is not the one already shown; null releases the texture. A backend without
    // filterable half-float textures, or a grid larger than they allow, keeps no texture (flat ambient).
    void Update(std::shared_ptr<const LightProbeGrid> grid);
    void Shutdown() noexcept;
    // Fills the lighting config's probe texture and uniforms, the grid placed relative to the render origin
    // the frame's positions are in; leaves the config untouched without a texture.
    void Apply(SceneRenderLightingConfig& config, const kb::math::DVec3& renderOrigin) const noexcept;

    // Whether this backend can sample the probe texture.
    [[nodiscard]] static bool Supported() noexcept;

private:
    std::shared_ptr<const LightProbeGrid> grid_;
    bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
};

// The sampler and uniforms of light_probes.sh, bound the same way by every pass that lights with the grid.
class LightProbeUniforms {
public:
    // `samplerName` is the sampler the shader reads the grid through: s_lightProbes, or s_ssrLit where the
    // deferred lighting shader shares that stage.
    void Create(const char* samplerName);
    void Destroy() noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    // Sets the uniforms (w of `size` is 1 when the grid is used) and binds `texture` at `stage`, or
    // `fallback` where the frame has none. With neither the stage stays as the caller bound it; returns
    // whether the stage was bound.
    bool Set(bgfx::TextureHandle texture, const std::array<float, 4>& grid, const std::array<float, 4>& size,
        std::uint8_t stage, bgfx::TextureHandle fallback) const;

private:
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle grid_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle size_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
