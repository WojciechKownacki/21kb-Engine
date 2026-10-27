#include "Camera2D.hpp"

#include <algorithm>
#include <cmath>

namespace kb::world2d {
namespace {

[[nodiscard]] bool Positive(float value) noexcept {
    return std::isfinite(value) && value > 0.0F;
}

[[nodiscard]] float DeadZoneAxis(float current, float target, float width) noexcept {
    const float reach = std::max(0.0F, width) * 0.5F;
    const float offset = target - current;
    if (std::abs(offset) <= reach) return current;
    return target - (offset > 0.0F ? reach : -reach);
}

} // namespace

PixelPerfectView ComputePixelPerfectView(float viewportHeightPixels, float pixelsPerUnit, float wantedHeightUnits) noexcept {
    PixelPerfectView view;
    view.orthographicHeight = wantedHeightUnits;
    if (!Positive(viewportHeightPixels) || !Positive(pixelsPerUnit) || !Positive(wantedHeightUnits)) return view;
    const double wantedPixels = static_cast<double>(wantedHeightUnits) * static_cast<double>(pixelsPerUnit);
    const double fit = std::floor(static_cast<double>(viewportHeightPixels) / wantedPixels);
    const double zoom = std::clamp(fit, 1.0, 1024.0);
    view.pixelPerfect = true;
    view.zoom = static_cast<std::uint32_t>(zoom);
    const double screenPixelsPerUnit = static_cast<double>(pixelsPerUnit) * zoom;
    view.orthographicHeight = static_cast<float>(static_cast<double>(viewportHeightPixels) / screenPixelsPerUnit);
    view.unitsPerPixel = static_cast<float>(1.0 / screenPixelsPerUnit);
    return view;
}

float SnapToPixels(float value, float unitsPerPixel) noexcept {
    if (!Positive(unitsPerPixel) || !std::isfinite(value)) return value;
    const double pixel = static_cast<double>(unitsPerPixel);
    return static_cast<float>(std::round(static_cast<double>(value) / pixel) * pixel);
}

float ClampCameraAxis(float value, float half, float low, float high) noexcept {
    const float reach = std::max(0.0F, half);
    if (high - low <= reach * 2.0F) return (low + high) * 0.5F;
    return std::clamp(value, low + reach, high - reach);
}

std::array<float, 2> StepCamera2DFollow(const Camera2DFollow& frame) noexcept {
    const auto clamped = [&](std::array<float, 2> position) {
        if (!frame.clamp) return position;
        position[0] = ClampCameraAxis(position[0], frame.halfWidth, frame.area.minU, frame.area.maxU);
        position[1] = ClampCameraAxis(position[1], frame.halfHeight, frame.area.minV, frame.area.maxV);
        return position;
    };
    if (!frame.started) return clamped(frame.target);
    const float du = frame.target[0] - frame.current[0];
    const float dv = frame.target[1] - frame.current[1];
    if (frame.teleportDistance > 0.0F && std::hypot(du, dv) > frame.teleportDistance) return clamped(frame.target);

    const std::array<float, 2> wanted{ DeadZoneAxis(frame.current[0], frame.target[0], frame.deadZoneWidth),
        DeadZoneAxis(frame.current[1], frame.target[1], frame.deadZoneHeight) };
    float blend = 1.0F;
    if (frame.smoothingSeconds > 0.0F) {
        blend = frame.deltaSeconds > 0.0F ? 1.0F - std::exp(-frame.deltaSeconds / frame.smoothingSeconds) : 0.0F;
    }
    return clamped({ frame.current[0] + (wanted[0] - frame.current[0]) * blend,
        frame.current[1] + (wanted[1] - frame.current[1]) * blend });
}

} // namespace kb::world2d
