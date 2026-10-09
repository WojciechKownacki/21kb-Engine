#pragma once

#include "engine/math/DVec3.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"

#include <array>
#include <optional>
#include <vector>

namespace kb::render {

// Camera-relative rendering (docs/large_worlds.md): conversions between world space, in which hosts describe their
// cameras and editor overlays, and render space, which is world space minus the render origin.
class RendererRenderOrigin {
public:
    RendererRenderOrigin() = delete;

    // The eye of a rigid column-major view matrix (world to view), in double precision.
    [[nodiscard]] static kb::math::DVec3 ViewEye(const std::array<float, 16>& view) noexcept;
    // `view` (world to view) as a render-space view for `origin`. With `eye`, the precise world position of the
    // viewer, the translation is rebuilt from it; otherwise it is carried over in double precision.
    [[nodiscard]] static std::array<float, 16> RelativeView(
        const std::array<float, 16>& view, const kb::math::DVec3& origin, const std::optional<kb::math::DVec3>& eye) noexcept;
    // A view-projection of positions relative to an origin, re-expressed for positions relative to an origin moved
    // by `delta` (the new origin minus the old one).
    [[nodiscard]] static std::array<float, 16> RebaseViewProjection(const std::array<float, 16>& viewProjection, const kb::math::DVec3& delta) noexcept;
    // A position relative to `from` (zero for a world position), re-expressed relative to `origin`.
    [[nodiscard]] static std::array<float, 3> Relative(const std::array<float, 3>& position, const kb::math::DVec3& from, const kb::math::DVec3& origin) noexcept;
};

// Render-space copies of the world-space editor overlays of a submit; the spans of the adjusted description point
// into this storage, which is reused from submit to submit.
class RendererRelativeOverlays {
public:
    void Apply(RenderSceneSubmitDesc& desc, const kb::math::DVec3& origin);

private:
    std::vector<EditorCameraWireframeDesc> cameraWireframes_;
    std::vector<EditorLightWireframeDesc> lightWireframes_;
    std::vector<EditorParticleIconDesc> particleIcons_;
    std::vector<PhysicsDebugLine> physicsDebugLines_;
};

} // namespace kb::render
