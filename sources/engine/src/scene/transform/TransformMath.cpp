#include "scene/transform/TransformMath.hpp"

#include "scene/transform/QuatMath.hpp"
#include "scene/transform/Vec3Math.hpp"

#include <cmath>

namespace kb::scene {
namespace {

constexpr float kFastPathEpsilon = 0.000001F;

[[nodiscard]] bool NearlyEqual(float lhs, float rhs) noexcept {
    return std::fabs(lhs - rhs) <= kFastPathEpsilon;
}

[[nodiscard]] bool IsUnitScale(Vec3 value) noexcept {
    return NearlyEqual(value.x, 1.0F) && NearlyEqual(value.y, 1.0F) && NearlyEqual(value.z, 1.0F);
}

[[nodiscard]] bool IsUniformScale(Vec3 value) noexcept {
    return NearlyEqual(value.x, value.y) && NearlyEqual(value.y, value.z);
}

[[nodiscard]] bool IsIdentityRotation(Quat value) noexcept {
    return NearlyEqual(value.x, 0.0F) && NearlyEqual(value.y, 0.0F) && NearlyEqual(value.z, 0.0F) && NearlyEqual(value.w, 1.0F);
}

} // namespace

TransformComponent TransformMath::Identity() noexcept {
    return TransformComponent{
        .localScale = Vec3{ 1.0F, 1.0F, 1.0F },
        .worldScale = Vec3{ 1.0F, 1.0F, 1.0F },
        .localVersion = 0,
        .parentVersion = 0,
        .worldVersion = 0,
        .worldDirty = false,
    };
}

bool TransformMath::CanUseTranslatedParentFastPath(const TransformComponent& parent) noexcept {
    return IsUnitScale(parent.worldScale) && IsIdentityRotation(parent.worldRotation);
}

bool TransformMath::CanUseUnrotatedParentFastPath(const TransformComponent& parent) noexcept {
    return IsIdentityRotation(parent.worldRotation);
}

bool TransformMath::CanUseUnitScaleParentFastPath(const TransformComponent& parent) noexcept {
    return IsUnitScale(parent.worldScale);
}

bool TransformMath::CanUseUniformScaleParentFastPath(const TransformComponent& parent) noexcept {
    return IsUniformScale(parent.worldScale);
}

bool TransformMath::CanUseStaticLocalRotationFastPath(const TransformComponent& local) noexcept {
    return IsIdentityRotation(local.localRotation);
}

TransformComponent TransformMath::ComposeRoot(const TransformComponent& local) noexcept {
    TransformComponent result = local;
    result.worldScale = local.localScale;
    result.worldRotation = QuatMath::Normalize(local.localRotation);
    result.worldPosition = local.localPosition;
    result.parentVersion = 0;
    result.worldVersion = local.worldVersion + 1U;
    result.worldDirty = false;
    return result;
}

TransformComponent TransformMath::ComposeTranslatedParent(const TransformComponent& parent, const TransformComponent& local) noexcept {
    TransformComponent result = local;
    result.worldScale = local.localScale;
    result.worldRotation = QuatMath::Normalize(local.localRotation);
    result.worldPosition = Vec3Math::Add(parent.worldPosition, local.localPosition);
    result.parentVersion = parent.worldVersion;
    result.worldVersion = local.worldVersion + 1U;
    result.worldDirty = false;
    return result;
}

TransformComponent TransformMath::ComposeUnrotatedParent(const TransformComponent& parent, const TransformComponent& local) noexcept {
    TransformComponent result = local;
    result.worldScale = Vec3Math::Multiply(parent.worldScale, local.localScale);
    result.worldRotation = QuatMath::Normalize(local.localRotation);
    result.worldPosition = Vec3Math::Add(parent.worldPosition, Vec3Math::Multiply(parent.worldScale, local.localPosition));
    result.parentVersion = parent.worldVersion;
    result.worldVersion = local.worldVersion + 1U;
    result.worldDirty = false;
    return result;
}

TransformComponent TransformMath::ComposeUnitScaleParent(const TransformComponent& parent, const TransformComponent& local) noexcept {
    TransformComponent result = local;
    result.worldScale = local.localScale;
    result.worldRotation = QuatMath::Normalize(QuatMath::Multiply(parent.worldRotation, local.localRotation));
    result.worldPosition = Vec3Math::Add(parent.worldPosition, Vec3Math::Rotate(parent.worldRotation, local.localPosition));
    result.parentVersion = parent.worldVersion;
    result.worldVersion = local.worldVersion + 1U;
    result.worldDirty = false;
    return result;
}

TransformComponent TransformMath::ComposeUniformScaleParent(const TransformComponent& parent, const TransformComponent& local) noexcept {
    const float parentScale = parent.worldScale.x;
    const Vec3 scaledLocalPosition{
        local.localPosition.x * parentScale,
        local.localPosition.y * parentScale,
        local.localPosition.z * parentScale,
    };
    TransformComponent result = local;
    result.worldScale = Vec3{
        parentScale * local.localScale.x,
        parentScale * local.localScale.y,
        parentScale * local.localScale.z,
    };
    result.worldRotation = QuatMath::Normalize(QuatMath::Multiply(parent.worldRotation, local.localRotation));
    result.worldPosition = Vec3Math::Add(parent.worldPosition, Vec3Math::Rotate(parent.worldRotation, scaledLocalPosition));
    result.parentVersion = parent.worldVersion;
    result.worldVersion = local.worldVersion + 1U;
    result.worldDirty = false;
    return result;
}

TransformComponent TransformMath::ComposeStaticLocalRotationParent(const TransformComponent& parent, const TransformComponent& local) noexcept {
    TransformComponent result = local;
    result.worldScale = Vec3Math::Multiply(parent.worldScale, local.localScale);
    result.worldRotation = QuatMath::Normalize(parent.worldRotation);
    result.worldPosition = Vec3Math::Add(parent.worldPosition, Vec3Math::Rotate(parent.worldRotation, Vec3Math::Multiply(parent.worldScale, local.localPosition)));
    result.parentVersion = parent.worldVersion;
    result.worldVersion = local.worldVersion + 1U;
    result.worldDirty = false;
    return result;
}

TransformComponent TransformMath::Compose(const TransformComponent& parent, const TransformComponent& local) noexcept {
    TransformComponent result = local;
    result.worldScale = Vec3Math::Multiply(parent.worldScale, local.localScale);
    result.worldRotation = QuatMath::Normalize(QuatMath::Multiply(parent.worldRotation, local.localRotation));
    result.worldPosition = Vec3Math::Add(parent.worldPosition, Vec3Math::Rotate(parent.worldRotation, Vec3Math::Multiply(parent.worldScale, local.localPosition)));
    result.parentVersion = parent.worldVersion;
    result.worldVersion = local.worldVersion + 1U;
    result.worldDirty = false;
    return result;
}

} // namespace kb::scene
