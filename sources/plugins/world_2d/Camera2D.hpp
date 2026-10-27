#pragma once

#include <array>
#include <cstdint>

namespace kb::world2d {

// The numbers of a pixel perfect view: the largest whole number of screen pixels per texel (zoom) that
// still shows the wanted height, and the orthographic height that makes exactly that many pixels.
struct PixelPerfectView {
    bool pixelPerfect = false;       // false: the wanted height is used as it is
    std::uint32_t zoom = 1U;         // screen pixels per texel
    float orthographicHeight = 0.0F; // world units from the bottom to the top of the viewport
    float unitsPerPixel = 0.0F;      // world units of one screen pixel; 0 when not pixel perfect
};

// `pixelsPerUnit` texels make one world unit. The view never magnifies less than one pixel per texel, so a
// viewport shorter than the wanted height shows less than wanted. Not pixel perfect (the wanted height) for
// a viewport, pixel density or wanted height that is not a positive finite number.
[[nodiscard]] PixelPerfectView ComputePixelPerfectView(float viewportHeightPixels, float pixelsPerUnit, float wantedHeightUnits) noexcept;

// The nearest position that lies on a whole screen pixel. Unchanged for a pixel size of zero.
[[nodiscard]] float SnapToPixels(float value, float unitsPerPixel) noexcept;

// Where a view of half-height `half` may be centred along one axis of an area [low, high]: inside it, or in
// the middle of it when the area is smaller than the view.
[[nodiscard]] float ClampCameraAxis(float value, float half, float low, float high) noexcept;

// A rectangle of the map plane: u runs across, v up the screen.
struct Camera2DArea {
    float minU = 0.0F;
    float minV = 0.0F;
    float maxU = 0.0F;
    float maxV = 0.0F;
};

struct Camera2DFollow {
    std::array<float, 2> current{};  // where the camera is centred now
    std::array<float, 2> target{};   // the thing it follows
    bool started = false;            // false on the first frame: the camera goes straight to the target
    float halfWidth = 0.0F;          // of the view, in world units
    float halfHeight = 0.0F;
    float deadZoneWidth = 0.0F;      // the target may roam this far across and up around the centre
    float deadZoneHeight = 0.0F;
    float smoothingSeconds = 0.0F;
    float deltaSeconds = 0.0F;
    float teleportDistance = 0.0F;   // a target further away than this is a teleport: no smoothing
    bool clamp = false;
    Camera2DArea area{};
};

// One frame of the follow camera: the drag margins first, then smoothing that does not depend on the frame
// rate (about two thirds of the distance in `smoothingSeconds`), then the area. A teleport, and the first
// frame, land on the target at once.
[[nodiscard]] std::array<float, 2> StepCamera2DFollow(const Camera2DFollow& frame) noexcept;

} // namespace kb::world2d
