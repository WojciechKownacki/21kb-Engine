#include "renderer/RendererRenderOrigin.hpp"

#include <span>

namespace kb::render {

kb::math::DVec3 RendererRenderOrigin::ViewEye(const std::array<float, 16>& view) noexcept {
    // For a rigid view V = [R | t] the eye E satisfies R * E + t = 0: E = -transpose(R) * t, where the rows of R are
    // the camera's right (0, 4, 8), up (1, 5, 9) and forward (2, 6, 10) axes.
    const double tx = view[12];
    const double ty = view[13];
    const double tz = view[14];
    return kb::math::DVec3{
        -(view[0] * tx + view[1] * ty + view[2] * tz),
        -(view[4] * tx + view[5] * ty + view[6] * tz),
        -(view[8] * tx + view[9] * ty + view[10] * tz),
    };
}

std::array<float, 16> RendererRenderOrigin::RelativeView(
    const std::array<float, 16>& view, const kb::math::DVec3& origin, const std::optional<kb::math::DVec3>& eye) noexcept {
    std::array<float, 16> relative = view;
    const auto rotate = [&view](const kb::math::DVec3& value, std::size_t row) noexcept {
        return view[row] * value.x + view[4U + row] * value.y + view[8U + row] * value.z;
    };
    for (std::size_t row = 0U; row < 3U; ++row) {
        // R * (p - origin) + t' = R * p + t: t' = t + R * origin, or -R * (eye - origin) from a precise eye.
        relative[12U + row] = static_cast<float>(eye.has_value() ? -rotate(*eye - origin, row) : static_cast<double>(view[12U + row]) + rotate(origin, row));
    }
    return relative;
}

std::array<float, 16> RendererRenderOrigin::RebaseViewProjection(const std::array<float, 16>& viewProjection, const kb::math::DVec3& delta) noexcept {
    std::array<float, 16> rebased = viewProjection;
    for (std::size_t row = 0U; row < 4U; ++row) {
        rebased[12U + row] = static_cast<float>(static_cast<double>(viewProjection[12U + row]) + viewProjection[row] * delta.x +
            viewProjection[4U + row] * delta.y + viewProjection[8U + row] * delta.z);
    }
    return rebased;
}

std::array<float, 3> RendererRenderOrigin::Relative(
    const std::array<float, 3>& position, const kb::math::DVec3& from, const kb::math::DVec3& origin) noexcept {
    const kb::math::Vec3 relative = kb::math::RelativeTo(from + kb::math::Vec3{ position[0], position[1], position[2] }, origin);
    return { relative.x, relative.y, relative.z };
}

void RendererRelativeOverlays::Apply(RenderSceneSubmitDesc& desc, const kb::math::DVec3& origin) {
    const kb::math::DVec3 from = desc.overlayOrigin;
    desc.editorGizmo.targetPosition = RendererRenderOrigin::Relative(desc.editorGizmo.targetPosition, from, origin);
    desc.editorGrid.worldOffset = origin;
    cameraWireframes_.assign(desc.editorCameraWireframes.begin(), desc.editorCameraWireframes.end());
    for (EditorCameraWireframeDesc& camera : cameraWireframes_) camera.position = RendererRenderOrigin::Relative(camera.position, from, origin);
    desc.editorCameraWireframes = cameraWireframes_;
    lightWireframes_.assign(desc.editorLightWireframes.begin(), desc.editorLightWireframes.end());
    for (EditorLightWireframeDesc& light : lightWireframes_) light.position = RendererRenderOrigin::Relative(light.position, from, origin);
    desc.editorLightWireframes = lightWireframes_;
    particleIcons_.assign(desc.editorParticleIcons.begin(), desc.editorParticleIcons.end());
    for (EditorParticleIconDesc& icon : particleIcons_) icon.position = RendererRenderOrigin::Relative(icon.position, from, origin);
    desc.editorParticleIcons = particleIcons_;
    physicsDebugLines_.assign(desc.physicsDebugLines.begin(), desc.physicsDebugLines.end());
    for (PhysicsDebugLine& line : physicsDebugLines_) {
        line.from = RendererRenderOrigin::Relative(line.from, from, origin);
        line.to = RendererRenderOrigin::Relative(line.to, from, origin);
    }
    desc.physicsDebugLines = physicsDebugLines_;
}

} // namespace kb::render
