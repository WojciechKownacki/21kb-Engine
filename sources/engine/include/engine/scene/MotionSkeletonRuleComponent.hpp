#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/SkeletonAsset.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::scene {

// Authored per-entity pose rule applied after controller rig constraints.
// The rule constrains bones of the entity's own skeleton instance; targets
// are transient IK target names resolved through the animator runtime, never
// serialized entity references.
enum class MotionSkeletonRuleKind : std::uint8_t {
    Aim,
    ChainIk,
    Twist,
    Limit,
    Spring,
    SpaceCorrection,
};

struct MotionSkeletonRuleComponent {
    static constexpr std::string_view StableId = "kb21.motion.skeleton-rule";
    static constexpr std::uint32_t SchemaVersion = 1U;
    static constexpr std::uint32_t MaxTargetNameBytes = 63U;

    MotionSkeletonRuleKind kind = MotionSkeletonRuleKind::Aim;
    // All kinds; for ChainIk this is the chain root bone.
    SkeletonBoneId constrainedBoneId = 0U;
    SkeletonBoneId midBoneId = 0U;    // ChainIk
    SkeletonBoneId tipBoneId = 0U;    // ChainIk
    SkeletonBoneId sourceBoneId = 0U; // Twist
    std::array<char, MaxTargetNameBytes + 1U> target{};     // Aim/ChainIk/SpaceCorrection
    std::array<char, MaxTargetNameBytes + 1U> poleTarget{}; // ChainIk optional
    std::uint32_t targetLength = 0U;
    std::uint32_t poleTargetLength = 0U;
    Vec3 axis{ 1.0F, 0.0F, 0.0F }; // Twist/Limit, bone-local space
    float minAngleDegrees = -180.0F; // Limit
    float maxAngleDegrees = 180.0F;  // Limit
    float halfLifeSeconds = 0.1F;    // Spring
    float weight = 1.0F;
    // An explicitly disabled rule is an editor draft and cannot drive a pose.
    // Runtime systems require IsMotionSkeletonRuleComponentValid().
    bool enabled = false;
};

[[nodiscard]] inline std::string_view MotionSkeletonRuleTargetText(const MotionSkeletonRuleComponent& component) noexcept {
    return std::string_view{ component.target.data(), component.targetLength };
}

[[nodiscard]] inline std::string_view MotionSkeletonRulePoleTargetText(const MotionSkeletonRuleComponent& component) noexcept {
    return std::string_view{ component.poleTarget.data(), component.poleTargetLength };
}

[[nodiscard]] inline bool MotionSkeletonRuleNameIsValid(std::string_view name) noexcept {
    if (name.size() > MotionSkeletonRuleComponent::MaxTargetNameBytes) {
        return false;
    }
    for (const char character : name) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte == 0U || byte < 0x20U || byte == 0x7FU) {
            return false;
        }
    }
    return true;
}

inline void SetMotionSkeletonRuleTargetText(MotionSkeletonRuleComponent& component, std::string_view name) noexcept {
    const std::uint32_t length = static_cast<std::uint32_t>(std::min<std::size_t>(name.size(), MotionSkeletonRuleComponent::MaxTargetNameBytes));
    std::fill(component.target.begin(), component.target.end(), '\0');
    for (std::uint32_t index = 0U; index < length; ++index) {
        component.target[index] = name[index];
    }
    component.targetLength = length;
}

inline void SetMotionSkeletonRulePoleTargetText(MotionSkeletonRuleComponent& component, std::string_view name) noexcept {
    const std::uint32_t length = static_cast<std::uint32_t>(std::min<std::size_t>(name.size(), MotionSkeletonRuleComponent::MaxTargetNameBytes));
    std::fill(component.poleTarget.begin(), component.poleTarget.end(), '\0');
    for (std::uint32_t index = 0U; index < length; ++index) {
        component.poleTarget[index] = name[index];
    }
    component.poleTargetLength = length;
}

[[nodiscard]] inline bool TrySetMotionSkeletonRuleTargetText(MotionSkeletonRuleComponent& component, std::string_view name) noexcept {
    if (!MotionSkeletonRuleNameIsValid(name)) {
        return false;
    }
    SetMotionSkeletonRuleTargetText(component, name);
    return true;
}

[[nodiscard]] inline bool TrySetMotionSkeletonRulePoleTargetText(MotionSkeletonRuleComponent& component, std::string_view name) noexcept {
    if (!MotionSkeletonRuleNameIsValid(name)) {
        return false;
    }
    SetMotionSkeletonRulePoleTargetText(component, name);
    return true;
}

[[nodiscard]] inline bool IsMotionSkeletonRuleComponentValid(const MotionSkeletonRuleComponent& value) noexcept {
    if (!value.enabled) {
        return false;
    }
    if (value.constrainedBoneId == 0U || !std::isfinite(value.weight) || value.weight < 0.0F) {
        return false;
    }
    if (!std::isfinite(value.axis.x) || !std::isfinite(value.axis.y) || !std::isfinite(value.axis.z) ||
        !std::isfinite(value.minAngleDegrees) || !std::isfinite(value.maxAngleDegrees) ||
        !std::isfinite(value.halfLifeSeconds)) {
        return false;
    }
    const bool axisNonZero = kb::math::Dot(value.axis, value.axis) > 1.0e-12F;
    switch (value.kind) {
    case MotionSkeletonRuleKind::Aim:
    case MotionSkeletonRuleKind::SpaceCorrection:
        return value.targetLength != 0U;
    case MotionSkeletonRuleKind::ChainIk:
        return value.midBoneId != 0U && value.tipBoneId != 0U && value.targetLength != 0U;
    case MotionSkeletonRuleKind::Twist:
        return value.sourceBoneId != 0U && axisNonZero;
    case MotionSkeletonRuleKind::Limit:
        return axisNonZero && value.minAngleDegrees <= value.maxAngleDegrees;
    case MotionSkeletonRuleKind::Spring:
        return value.halfLifeSeconds > 0.0F;
    }
    return false;
}

[[nodiscard]] inline bool IsMotionSkeletonRuleComponentPersistable(const MotionSkeletonRuleComponent& value) noexcept {
    return IsMotionSkeletonRuleComponentValid(value) ||
        (!value.enabled && value.constrainedBoneId == 0U && value.targetLength == 0U && value.poleTargetLength == 0U);
}

} // namespace kb::scene
