#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstddef>

namespace kb::scene {

class SceneTransformRootHotKernel {
public:
    SceneTransformRootHotKernel() = delete;

    [[nodiscard]] static constexpr bool CanApplyIdentityRotationFastPath(const TransformComponent& transform) noexcept {
        return transform.localRotation.x == 0.0F &&
            transform.localRotation.y == 0.0F &&
            transform.localRotation.z == 0.0F &&
            transform.localRotation.w == 1.0F;
    }

    [[nodiscard]] static constexpr bool CanWriteIdentityAffineFastPath(const TransformComponent& transform) noexcept {
        return transform.worldRotation.x == 0.0F &&
            transform.worldRotation.y == 0.0F &&
            transform.worldRotation.z == 0.0F &&
            transform.worldRotation.w == 1.0F &&
            transform.worldScale.x == 1.0F &&
            transform.worldScale.y == 1.0F &&
            transform.worldScale.z == 1.0F;
    }

    static constexpr void ApplyIdentityRotationRoot(TransformComponent& transform) noexcept {
        transform.worldScale = transform.localScale;
        transform.worldRotation = transform.localRotation;
        transform.worldPosition = transform.localPosition;
        transform.parentVersion = 0;
        transform.worldVersion = transform.worldVersion + 1U;
        transform.worldDirty = false;
    }

    static void ApplyIdentityRotationRoots(TransformComponent* transforms, std::size_t count) noexcept {
        if (transforms == nullptr) {
            return;
        }
        for (std::size_t index = 0U; index < count; ++index) {
            if (CanApplyIdentityRotationFastPath(transforms[index])) {
                ApplyIdentityRotationRoot(transforms[index]);
            }
        }
    }

    static constexpr void WriteIdentityAffine(const TransformComponent& transform, WorldTransformAffine3x4& affine) noexcept {
        affine.values[0] = 1.0F;
        affine.values[1] = 0.0F;
        affine.values[2] = 0.0F;
        affine.values[3] = 0.0F;
        affine.values[4] = 1.0F;
        affine.values[5] = 0.0F;
        affine.values[6] = 0.0F;
        affine.values[7] = 0.0F;
        affine.values[8] = 1.0F;
        affine.values[9] = transform.worldPosition.x;
        affine.values[10] = transform.worldPosition.y;
        affine.values[11] = transform.worldPosition.z;
    }

    static constexpr void WriteIdentityRotationAffine(const TransformComponent& transform, WorldTransformAffine3x4& affine) noexcept {
        affine.values[0] = transform.worldScale.x;
        affine.values[1] = 0.0F;
        affine.values[2] = 0.0F;
        affine.values[3] = 0.0F;
        affine.values[4] = transform.worldScale.y;
        affine.values[5] = 0.0F;
        affine.values[6] = 0.0F;
        affine.values[7] = 0.0F;
        affine.values[8] = transform.worldScale.z;
        affine.values[9] = transform.worldPosition.x;
        affine.values[10] = transform.worldPosition.y;
        affine.values[11] = transform.worldPosition.z;
    }

    [[nodiscard]] static std::size_t WriteIdentityAffineBatch(
        const TransformComponent* transforms,
        WorldTransformAffine3x4* affines,
        std::size_t count) noexcept {
        if (transforms == nullptr || affines == nullptr) {
            return 0U;
        }

        std::size_t identityAffineCount = 0U;
        for (std::size_t index = 0U; index < count; ++index) {
            if (!CanWriteIdentityAffineFastPath(transforms[index])) {
                continue;
            }
            WriteIdentityAffine(transforms[index], affines[index]);
            ++identityAffineCount;
        }
        return identityAffineCount;
    }
};

} // namespace kb::scene
