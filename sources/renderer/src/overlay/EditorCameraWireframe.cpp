#include "kb/render/overlay/EditorCameraWireframe.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {
namespace {

constexpr float kPi = 3.14159265358979323846F;

using CameraPlaneCorners = std::array<std::array<float, 3>, 4U>;

[[nodiscard]] std::array<float, 3> Add(
    std::array<float, 3> lhs,
    std::array<float, 3> rhs) noexcept {
    return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
}

[[nodiscard]] std::array<float, 3> Mul(
    std::array<float, 3> value,
    float scale) noexcept {
    return {value[0] * scale, value[1] * scale, value[2] * scale};
}

[[nodiscard]] CameraPlaneCorners CameraPlane(
    const EditorCameraWireframeDesc& camera,
    float depth,
    float halfWidth,
    float halfHeight) noexcept {
    const std::array<float, 3> center =
        Add(camera.position, Mul(camera.forward, depth));
    const std::array<float, 3> horizontal = Mul(camera.right, halfWidth);
    const std::array<float, 3> vertical = Mul(camera.up, halfHeight);
    return CameraPlaneCorners{{
        Add(center, Add(Mul(horizontal, -1.0F), Mul(vertical, -1.0F))),
        Add(center, Add(horizontal, Mul(vertical, -1.0F))),
        Add(center, Add(horizontal, vertical)),
        Add(center, Add(Mul(horizontal, -1.0F), vertical)),
    }};
}

} // namespace

std::array<EditorCameraWireframeLine, kEditorCameraWireframeLineCount>
BuildEditorCameraWireframeLines(
    const EditorCameraWireframeDesc& camera) noexcept {
    const float nearClip = std::max(0.0001F, camera.nearClip);
    const float authoredFarClip =
        std::max(nearClip + 0.0001F, camera.farClip);
    const float farClip = camera.displayFarClip > nearClip
        ? std::min(authoredFarClip, camera.displayFarClip)
        : authoredFarClip;
    const float aspect = std::max(0.0001F, camera.aspect);

    float nearHalfHeight = 0.0F;
    float farHalfHeight = 0.0F;
    if (camera.projection == EditorCameraWireframeProjection::Orthographic) {
        nearHalfHeight =
            std::max(0.0001F, camera.orthographicHeight) * 0.5F;
        farHalfHeight = nearHalfHeight;
    } else {
        const float fovRadians =
            std::clamp(camera.verticalFovDegrees, 1.0F, 179.0F) *
            0.5F * kPi / 180.0F;
        const float slope = std::tan(fovRadians);
        nearHalfHeight = slope * nearClip;
        farHalfHeight = slope * farClip;
    }

    const CameraPlaneCorners nearPlane =
        CameraPlane(camera, nearClip, nearHalfHeight * aspect, nearHalfHeight);
    const CameraPlaneCorners farPlane =
        CameraPlane(camera, farClip, farHalfHeight * aspect, farHalfHeight);

    std::array<EditorCameraWireframeLine, kEditorCameraWireframeLineCount>
        lines{};
    for (std::size_t corner = 0U; corner < nearPlane.size(); ++corner) {
        const std::size_t nextCorner = (corner + 1U) % nearPlane.size();
        lines[corner] = EditorCameraWireframeLine{
            .from = nearPlane[corner],
            .to = nearPlane[nextCorner],
            .alpha = 0.92F,
        };
        lines[4U + corner] = EditorCameraWireframeLine{
            .from = farPlane[corner],
            .to = farPlane[nextCorner],
            .alpha = 0.72F,
        };
        lines[8U + corner] = EditorCameraWireframeLine{
            .from = nearPlane[corner],
            .to = farPlane[corner],
            .alpha = 0.86F,
        };
    }
    return lines;
}

} // namespace kb::render
