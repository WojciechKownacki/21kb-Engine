#pragma once

#include "app/scene_viewport/EditorSceneViewportSelectionTypes.hpp"
#include "app/scene_viewport/EditorSceneViewportTypes.hpp"

namespace kb::editor {

#if defined(_WIN32)

// Distance in pixels from a click to the screen segment a-b; `along` receives where on the
// segment (0 at a, 1 at b) the nearest point lies.
[[nodiscard]] float EditorViewportScreenSegmentDistance(
    float pointX, float pointY, float ax, float ay, float bx, float by, float& along) noexcept;

// How far from a drawn editor line a click still selects it, in pixels, at the given display scale.
[[nodiscard]] float EditorViewportWirePickTolerancePixels(float dpiScale) noexcept;

// The nearest editor-only overlay under a click: a line of a wire shape (light range, camera
// frustum, collider, joint) or an icon. `pixelDistance` is from the click to the line or icon on
// screen; `depth` is the camera distance of that nearest point, in the units of a mesh ray hit.
struct EditorViewportOverlayHit {
    kb::scene::SceneEntity entity{};
    float pixelDistance = 0.0F;
    float depth = 0.0F;

    [[nodiscard]] bool IsValid() const noexcept {
        return entity.IsValid();
    }
};

// The closer line on screen wins; a tie on screen goes to the overlay nearer the camera.
[[nodiscard]] bool EditorViewportOverlayHitIsBetter(
    const EditorViewportOverlayHit& candidate, const EditorViewportOverlayHit& current) noexcept;

// What a click selects: an overlay line or icon, unless real geometry under the cursor is nearer
// the camera than it. The space inside a wire shape is never a hit.
[[nodiscard]] EditorSceneViewportPickResult ResolveViewportClickPick(
    const EditorSceneViewportPickResult& geometry, const EditorViewportOverlayHit& overlay) noexcept;

// Collects the nearest overlay hit of one click. Every wire shape and icon goes through it, so all
// editor-only overlays pick by one rule.
class EditorViewportOverlayPicker {
public:
    EditorViewportOverlayPicker(
        const EditorViewportCameraState& camera, const RECT& renderArea, float clickX, float clickY, float tolerancePixels) noexcept;

    // A straight line of a wire shape, in viewport space. The part behind the camera is ignored.
    void ConsiderLine(kb::scene::SceneEntity entity, kb::scene::Vec3 from, kb::scene::Vec3 to) noexcept;
    // An icon drawn at `position`, `halfSizePixels` from its centre to each side.
    void ConsiderIcon(kb::scene::SceneEntity entity, kb::scene::Vec3 position, float halfSizePixels) noexcept;

    [[nodiscard]] const EditorViewportOverlayHit& Best() const noexcept {
        return best_;
    }

private:
    void Offer(const EditorViewportOverlayHit& candidate) noexcept;

    const EditorViewportCameraState* camera_ = nullptr;
    EditorViewportCameraAxes axes_{};
    RECT renderArea_{};
    float clickX_ = 0.0F;
    float clickY_ = 0.0F;
    float tolerancePixels_ = 0.0F;
    EditorViewportOverlayHit best_{};
};

#endif

} // namespace kb::editor
