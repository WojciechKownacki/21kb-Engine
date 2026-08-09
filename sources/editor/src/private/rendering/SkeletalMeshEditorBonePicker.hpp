#pragma once

#include "scene/EditorAnimationPreviewScene.hpp"
#include "scene/EditorViewportCameraState.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>

namespace kb::editor {

struct SkeletalMeshEditorBonePickViewport {
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

class SkeletalMeshEditorBonePicker final {
public:
    [[nodiscard]] static std::optional<kb::scene::SkeletonBoneId> Pick(
        const SkeletalMeshEditorBonePickViewport& viewport,
        const EditorViewportCameraState& camera,
        std::span<const AnimationPreviewOverlayLine> lines,
        float pointerX,
        float pointerY) noexcept {
        if (viewport.width <= 0.0F || viewport.height <= 0.0F) return std::nullopt;

        const EditorViewportCameraAxes axes = camera.Axes();
        const float tangent = std::tan(camera.VerticalFovDegrees() * 0.00872664626F);
        const float aspect = viewport.width / viewport.height;
        const auto project = [&](kb::scene::Vec3 point, float& screenX, float& screenY) {
            const kb::scene::Vec3 delta = point - axes.position;
            const float depth = Dot(delta, axes.forward);
            if (depth <= 0.001F) return false;
            const float horizontal = Dot(delta, axes.right) / (depth * tangent * aspect);
            const float vertical = Dot(delta, axes.up) / (depth * tangent);
            screenX = viewport.left + (horizontal * 0.5F + 0.5F) * viewport.width;
            screenY = viewport.top + (0.5F - vertical * 0.5F) * viewport.height;
            return true;
        };

        // Joint markers are the least ambiguous target: a child line ends at its own bone, while
        // its start belongs to the parent. Give joints priority over the wider shaft target so a
        // click at a branch consistently selects the bone whose pivot the programmer clicked.
        constexpr float kJointRadiusSquared = 14.0F * 14.0F;
        std::optional<kb::scene::SkeletonBoneId> closestJoint;
        float closestJointDistance = kJointRadiusSquared;
        for (const AnimationPreviewOverlayLine& line : lines) {
            ConsiderJoint(project, line.to, line.boneId, pointerX, pointerY,
                closestJointDistance, closestJoint);
            ConsiderJoint(project, line.from, line.fromBoneId, pointerX, pointerY,
                closestJointDistance, closestJoint);
        }
        if (closestJoint.has_value()) return closestJoint;

        constexpr float kShaftRadiusSquared = 12.0F * 12.0F;
        std::optional<kb::scene::SkeletonBoneId> closestShaft;
        float closestShaftDistance = kShaftRadiusSquared;
        for (const AnimationPreviewOverlayLine& line : lines) {
            if (line.boneId == 0U) continue;
            float fromX = 0.0F;
            float fromY = 0.0F;
            float toX = 0.0F;
            float toY = 0.0F;
            if (!project(line.from, fromX, fromY) || !project(line.to, toX, toY)) continue;
            const float distance = PointSegmentDistanceSquared(
                pointerX, pointerY, fromX, fromY, toX, toY);
            if (distance < closestShaftDistance) {
                closestShaftDistance = distance;
                closestShaft = line.boneId;
            }
        }
        return closestShaft;
    }

private:
    [[nodiscard]] static float Dot(kb::scene::Vec3 left, kb::scene::Vec3 right) noexcept {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    [[nodiscard]] static float PointSegmentDistanceSquared(
        float pointX,
        float pointY,
        float fromX,
        float fromY,
        float toX,
        float toY) noexcept {
        const float deltaX = toX - fromX;
        const float deltaY = toY - fromY;
        const float lengthSquared = deltaX * deltaX + deltaY * deltaY;
        const float parameter = lengthSquared <= 0.0001F
            ? 0.0F
            : std::clamp(
                ((pointX - fromX) * deltaX + (pointY - fromY) * deltaY) / lengthSquared,
                0.0F,
                1.0F);
        const float nearestX = fromX + parameter * deltaX;
        const float nearestY = fromY + parameter * deltaY;
        const float distanceX = pointX - nearestX;
        const float distanceY = pointY - nearestY;
        return distanceX * distanceX + distanceY * distanceY;
    }

    template <typename Project>
    static void ConsiderJoint(
        const Project& project,
        kb::scene::Vec3 position,
        kb::scene::SkeletonBoneId boneId,
        float pointerX,
        float pointerY,
        float& closestDistance,
        std::optional<kb::scene::SkeletonBoneId>& closest) noexcept {
        if (boneId == 0U) return;
        float screenX = 0.0F;
        float screenY = 0.0F;
        if (!project(position, screenX, screenY)) return;
        const float deltaX = pointerX - screenX;
        const float deltaY = pointerY - screenY;
        const float distance = deltaX * deltaX + deltaY * deltaY;
        if (distance < closestDistance) {
            closestDistance = distance;
            closest = boneId;
        }
    }
};

} // namespace kb::editor
