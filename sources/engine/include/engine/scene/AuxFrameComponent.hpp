#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cmath>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// A CameraComponent and TransformComponent on the same entity provide the
// canonical optical and pose data. This component only configures the derived
// secondary render submission and its renderer-owned image target.
enum class AuxFrameMode : std::uint8_t {
    Flat,
    Mirror,
    Cube,
    // A portable 3x2 360-degree cubemap atlas. This avoids a backend-specific
    // conversion shader while preserving every captured face in one image.
    Panoramic,
};

struct AuxFrameComponent {
    static constexpr std::string_view StableId = "kb21.view.aux-frame";
    static constexpr std::uint32_t SchemaVersion = 1U;

    AuxFrameMode mode = AuxFrameMode::Flat;
    // Stable author-selected image target key. GPU textures and framebuffers
    // are derived renderer resources, never scene data.
    std::uint64_t imageTargetId = 0U;
    std::uint16_t width = 512U;
    std::uint16_t height = 512U;
    // World-space plane n dot p + d = 0 used only by Mirror mode. The owner
    // transform remains the sole source of camera pose.
    Vec3 mirrorPlaneNormal{ 0.0F, 1.0F, 0.0F };
    float mirrorPlaneOffset = 0.0F;
    bool enabled = false;
};

[[nodiscard]] constexpr bool IsAuxFrameModeValid(AuxFrameMode value) noexcept {
    return value == AuxFrameMode::Flat || value == AuxFrameMode::Mirror ||
        value == AuxFrameMode::Cube || value == AuxFrameMode::Panoramic;
}

[[nodiscard]] inline bool IsAuxFrameComponentValid(const AuxFrameComponent& value) noexcept {
    const float normalLengthSquared = value.mirrorPlaneNormal.x * value.mirrorPlaneNormal.x +
        value.mirrorPlaneNormal.y * value.mirrorPlaneNormal.y +
        value.mirrorPlaneNormal.z * value.mirrorPlaneNormal.z;
    return IsAuxFrameModeValid(value.mode) && value.imageTargetId != 0U &&
        value.width > 0U && value.height > 0U &&
        std::isfinite(value.mirrorPlaneNormal.x) && std::isfinite(value.mirrorPlaneNormal.y) &&
        std::isfinite(value.mirrorPlaneNormal.z) && std::isfinite(value.mirrorPlaneOffset) &&
        std::isfinite(normalLengthSquared) && normalLengthSquared > 1.0e-8F;
}

[[nodiscard]] inline bool IsAuxFrameComponentPersistable(const AuxFrameComponent& value) noexcept {
    if (IsAuxFrameComponentValid(value)) {
        return true;
    }
    const float normalLengthSquared = value.mirrorPlaneNormal.x * value.mirrorPlaneNormal.x +
        value.mirrorPlaneNormal.y * value.mirrorPlaneNormal.y +
        value.mirrorPlaneNormal.z * value.mirrorPlaneNormal.z;
    return !value.enabled && IsAuxFrameModeValid(value.mode) && value.imageTargetId == 0U &&
        value.width > 0U && value.height > 0U && std::isfinite(value.mirrorPlaneNormal.x) &&
        std::isfinite(value.mirrorPlaneNormal.y) && std::isfinite(value.mirrorPlaneNormal.z) &&
        std::isfinite(value.mirrorPlaneOffset) && std::isfinite(normalLengthSquared) && normalLengthSquared > 1.0e-8F;
}

} // namespace kb::scene
