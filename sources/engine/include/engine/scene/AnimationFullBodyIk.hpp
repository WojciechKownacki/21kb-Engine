#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/SkeletonAsset.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace kb::scene {

// The limbs of a body, in the order of FullBodyIkSettings::limbs: the legs first, since their targets keep
// priority over the arms'.
inline constexpr std::size_t kFullBodyIkLimbCount = 4U;
inline constexpr std::size_t kFullBodyIkLegCount = 2U;
// The bone chains of a body: the limbs, then the spine up to the head.
inline constexpr std::size_t kFullBodyIkChainCount = kFullBodyIkLimbCount + 1U;
inline constexpr std::size_t kFullBodyIkSpineChain = kFullBodyIkLimbCount;
// The bones a body names besides the hips: spine and head, then upper and tip bone of every limb.
inline constexpr std::size_t kFullBodyIkBoneCount = 2U + 2U * kFullBodyIkLimbCount;

struct FullBodyIkLimbSettings {
    // The thigh or upper arm, and the foot or hand. Both zero leaves the limb out.
    SkeletonBoneId upperBoneId = 0U;
    SkeletonBoneId tipBoneId = 0U;
    // IK targets set by name (Animator.SetIKTarget): where the foot or hand goes, and the side the knee or elbow
    // bends toward. Empty for none.
    std::string target;
    std::string poleTarget;
};

// Whole-body IK for a humanoid skeleton. The hips move so that every limb target is within reach, the limbs
// solve to their targets, and the spine and head turn toward a look target.
struct FullBodyIkSettings {
    // The lowest spine bone and the head bone; both zero for no look at. Bones between them share the turn.
    SkeletonBoneId spineBoneId = 0U;
    SkeletonBoneId headBoneId = 0U;
    // Left leg, right leg, left arm, right arm.
    std::array<FullBodyIkLimbSettings, kFullBodyIkLimbCount> limbs;
    // Pulls the hips to the target's position and orientation. Empty for none.
    std::string hipsTarget;
    // The point the head looks at. Empty for none.
    std::string lookTarget;
    // The head bone's own axis that points forward, and the most the head may turn away from the animation.
    kb::math::Vec3 lookAxis{ 0.0F, 0.0F, 1.0F };
    float lookLimitDegrees = 80.0F;
    // Foot placement: a ray down from each leg finds the ground, the foot follows its height and slope, and the
    // hips go down (or up) with the lowest foot. A leg with a target of its own keeps that target.
    bool footPlacement = false;
    // How far above the animated foot each ray starts; it reaches twice as far down.
    float groundProbeDistance = 0.5F;
    // The physics layers the ground may be on (every layer by default).
    std::uint32_t groundLayerMask = 0x7FFFFFFFU;
};

[[nodiscard]] bool IsFullBodyIkSettingsValid(const FullBodyIkSettings& settings) noexcept;

// Every bone the settings name, zero where unset, in a fixed order.
[[nodiscard]] std::array<SkeletonBoneId, kFullBodyIkBoneCount> FullBodyIkBones(const FullBodyIkSettings& settings) noexcept;

// Whether `name` is a target the settings read.
[[nodiscard]] bool FullBodyIkUsesTarget(const FullBodyIkSettings& settings, std::string_view name) noexcept;

// The first and last bone of one of the chains (limb 0-3, then kFullBodyIkSpineChain).
struct FullBodyIkChainBones {
    SkeletonBoneId root = 0U;
    SkeletonBoneId tip = 0U;
};
[[nodiscard]] FullBodyIkChainBones FullBodyIkChain(const FullBodyIkSettings& settings, std::size_t chain) noexcept;

// A limb that has to reach `target` from `root` with segments `length` long in total. A priority limb keeps its
// target when the others cannot be satisfied with it.
struct FullBodyReach {
    kb::math::Vec3 root{};
    kb::math::Vec3 target{};
    float length = 0.0F;
    bool priority = false;
};

// A move of the hips, starting from `initial` and changed as little as it takes, after which every limb is
// within reach of its target: all limbs are settled together, then the priority ones alone, so with targets that
// cannot all be met the priority ones hold. Allocation free.
[[nodiscard]] kb::math::Vec3 SolveFullBodyHipsShift(kb::math::Vec3 initial, std::span<const FullBodyReach> reaches) noexcept;

} // namespace kb::scene
