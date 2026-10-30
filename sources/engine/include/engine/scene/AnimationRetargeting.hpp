#pragma once

#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SkeletonAsset.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

// A bone's role whatever tool named it: the side ("l"/"r") and the body part, with the common
// synonyms of Mixamo, Unreal, Unity and Blender/Rigify folded together - "mixamorig:LeftForeArm",
// "lowerarm_l" and "forearm.L" are all "l:forearm"; "Hips" and "pelvis" are both "hips".
[[nodiscard]] std::string CanonicalBoneName(std::string_view name);

struct SkeletonBoneMapping {
    // For each target bone (by index), the source bone index it follows, or -1.
    std::vector<std::int32_t> sourceOfTarget;
    std::size_t mappedCount = 0U;
};

// Target bones find their source bone by exact name, then by CanonicalBoneName (only when one
// source bone has that name).
[[nodiscard]] SkeletonBoneMapping MapSkeletonBones(const SkeletonAsset& source, const SkeletonAsset& target);

// A saved mapping is tied to the complete rest poses and bone order of both rigs.
// -1 leaves a target bone in its reference pose; other values index the source rig.
struct AnimationRetargetProfile {
    std::uint64_t sourceSignature = 0U;
    std::uint64_t targetSignature = 0U;
    std::vector<std::int32_t> sourceOfTarget;
};
[[nodiscard]] AnimationRetargetProfile MakeAnimationRetargetProfile(const SkeletonAsset& source, const SkeletonAsset& target);
[[nodiscard]] std::string SerializeAnimationRetargetProfile(const AnimationRetargetProfile& profile);
[[nodiscard]] std::optional<AnimationRetargetProfile> ParseAnimationRetargetProfile(std::string_view text);

struct AnimationRetargetReport {
    std::size_t mappedBones = 0U;
    std::size_t targetBones = 0U;
    // Source bones the clip animates that no target bone follows.
    std::vector<std::string> unmappedAnimatedBones;
    float translationScale = 1.0F;
};

// Plays `clip`, made for `source`, on `target`: every mapped bone takes the source bone's rotation
// change from its rest pose in character space (so rigs whose bones point different ways still
// agree), the hips - and the bones above them - move by the source's motion scaled by the ratio of
// the two hip heights, and every other bone keeps the target's own proportions. Root motion follows
// the mapped root bone. Empty (with `error`) when fewer than half of the animated bones map.
[[nodiscard]] std::optional<AnimationClip> RetargetAnimationClip(
    const AnimationClip& clip, const SkeletonAsset& source,
    const SkeletonAsset& target, std::uint64_t targetSkeletonAssetId, std::uint64_t targetCompatibilitySignature,
    AnimationRetargetReport* report = nullptr, std::string* error = nullptr);

// Explicit profiles reject stale rig signatures and out-of-range mappings before sampling.
[[nodiscard]] std::optional<AnimationClip> RetargetAnimationClip(
    const AnimationClip& clip, const SkeletonAsset& source, const SkeletonAsset& target,
    std::uint64_t targetSkeletonAssetId, std::uint64_t targetCompatibilitySignature,
    const AnimationRetargetProfile& profile, AnimationRetargetReport* report = nullptr, std::string* error = nullptr);

} // namespace kb::scene
