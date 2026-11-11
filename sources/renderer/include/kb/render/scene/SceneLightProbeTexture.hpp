#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/render/StaticLightmapBaker.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <memory>

namespace kb::render {

// The GPU form of a baked light probe grid (light_probes.sh): one RGBA16F 3D texture of the grid's size
// with its 7 coefficient slots stacked along z. One texture keeps the forward mesh shader within the
// sampler stages every backend has.
class SceneLightProbeTexture {
public:
    SceneLightProbeTexture() = default;
    ~SceneLightProbeTexture();

    SceneLightProbeTexture(const SceneLightProbeTexture&) = delete;
    SceneLightProbeTexture& operator=(const SceneLightProbeTexture&) = delete;

    static constexpr std::uint32_t kSlots = 7U;

    // Uploads `grid` when it is not the one already shown; null releases the texture. A backend without
    // filterable half-float 3D textures, or a grid larger than they allow, keeps no texture (flat ambient).
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

} // namespace kb::render
