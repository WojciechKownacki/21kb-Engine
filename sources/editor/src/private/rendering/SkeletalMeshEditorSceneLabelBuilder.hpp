#pragma once

#include "rendering/EditorSceneViewportTextOverlay.hpp"
#include "scene/EditorAnimationPreviewScene.hpp"
#include "scene/EditorViewportCameraState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::editor {

// Projects scene labels into the transparent native overlay. Bone names are drawn small,
// white, with a one-pixel black shadow, so they stay readable over any mesh.
// The overlay performs the actual font rasterization; this builder owns only projection,
// distance scaling and semantic colors.
class SkeletalMeshEditorSceneLabelBuilder {
public:
    static void Append(
        std::vector<EditorSceneViewportTextLabel>& output,
        std::span<const AnimationPreviewOverlayLabel> labels,
        const EditorViewportCameraState& camera,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        float referenceCameraDistance) {
        if (viewportWidth == 0U || viewportHeight == 0U || labels.empty() ||
            !std::isfinite(referenceCameraDistance) || referenceCameraDistance <= 0.0F) return;

        const EditorViewportCameraAxes axes = camera.Axes();
        const float halfFovTangent = std::tan(
            camera.VerticalFovDegrees() * 0.00872664625997164788F);
        if (!std::isfinite(halfFovTangent) || halfFovTangent <= 0.0F) return;
        const float aspect = static_cast<float>(viewportWidth) /
            static_cast<float>(viewportHeight);
        constexpr float kSmallFontPixelHeight = 10.0F;
        constexpr std::size_t kMaximumLabels = 1024U;
        output.reserve(output.size() + std::min(labels.size(), kMaximumLabels));

        for (const AnimationPreviewOverlayLabel& label : labels) {
            if (output.size() >= kMaximumLabels || label.text.empty()) return;
            const kb::scene::Vec3 delta = label.position - axes.position;
            const float depth = Dot(delta, axes.forward);
            if (!std::isfinite(depth) || depth <= camera.NearClip()) continue;
            const float horizontal = Dot(delta, axes.right) /
                (depth * halfFovTangent * aspect);
            const float vertical = Dot(delta, axes.up) /
                (depth * halfFovTangent);
            if (!std::isfinite(horizontal) || !std::isfinite(vertical) ||
                horizontal < -1.0F || horizontal > 1.0F ||
                vertical < -1.0F || vertical > 1.0F) continue;

            output.push_back(EditorSceneViewportTextLabel{
                .x = (horizontal * 0.5F + 0.5F) * static_cast<float>(viewportWidth),
                .y = (0.5F - vertical * 0.5F) * static_cast<float>(viewportHeight),
                .pixelHeight = kSmallFontPixelHeight * referenceCameraDistance / depth,
                .text = label.text,
                .color = LabelColor(label.kind),
            });
        }
    }

private:
    [[nodiscard]] static constexpr float Dot(
        kb::scene::Vec3 left, kb::scene::Vec3 right) noexcept {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    [[nodiscard]] static constexpr std::array<std::uint8_t, 4U> LabelColor(
        AnimationPreviewOverlayLabelKind kind) noexcept {
        switch (kind) {
        case AnimationPreviewOverlayLabelKind::Socket:
            return { 71U, 235U, 199U, 255U };
        case AnimationPreviewOverlayLabelKind::Diagnostic:
            return { 255U, 219U, 56U, 255U };
        case AnimationPreviewOverlayLabelKind::Bone:
        default:
            return { 255U, 255U, 255U, 255U };
        }
    }
};

} // namespace kb::editor
