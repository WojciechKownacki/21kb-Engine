#include "engine/scene/AnimationFullBodyIk.hpp"

#include <algorithm>
#include <cmath>

namespace kb::scene {
namespace {

using kb::math::Vec3;

constexpr std::uint32_t kMaxPasses = 32U;
// A limb out of reach is brought back just inside it, so it ends bent rather than locked straight.
constexpr float kReachFraction = 0.999F;

} // namespace

bool IsFullBodyIkSettingsValid(const FullBodyIkSettings& settings) noexcept {
    const std::array<SkeletonBoneId, kFullBodyIkBoneCount> bones = FullBodyIkBones(settings);
    for (std::size_t index = 0U; index < bones.size(); ++index) {
        if (bones[index] != 0U && std::find(bones.begin() + static_cast<std::ptrdiff_t>(index) + 1, bones.end(), bones[index]) != bones.end()) {
            return false;
        }
    }
    const bool look = settings.spineBoneId != 0U;
    if (look != (settings.headBoneId != 0U) || (!look && !settings.lookTarget.empty())) return false;
    for (const FullBodyIkLimbSettings& limb : settings.limbs) {
        const bool used = limb.upperBoneId != 0U;
        if (used != (limb.tipBoneId != 0U) || (!used && !(limb.target.empty() && limb.poleTarget.empty()))) return false;
    }
    return std::isfinite(settings.lookAxis.x) && std::isfinite(settings.lookAxis.y) && std::isfinite(settings.lookAxis.z) &&
        kb::math::Dot(settings.lookAxis, settings.lookAxis) > 1.0e-12F && std::isfinite(settings.lookLimitDegrees) &&
        settings.lookLimitDegrees > 0.0F && settings.lookLimitDegrees <= 180.0F && std::isfinite(settings.groundProbeDistance) &&
        settings.groundProbeDistance > 0.0F && settings.groundProbeDistance <= 1000.0F && settings.groundLayerMask != 0U;
}

std::array<SkeletonBoneId, kFullBodyIkBoneCount> FullBodyIkBones(const FullBodyIkSettings& settings) noexcept {
    std::array<SkeletonBoneId, kFullBodyIkBoneCount> bones{ settings.spineBoneId, settings.headBoneId };
    for (std::size_t index = 0U; index < kFullBodyIkLimbCount; ++index) {
        bones[2U + 2U * index] = settings.limbs[index].upperBoneId;
        bones[3U + 2U * index] = settings.limbs[index].tipBoneId;
    }
    return bones;
}

bool FullBodyIkUsesTarget(const FullBodyIkSettings& settings, std::string_view name) noexcept {
    return settings.hipsTarget == name || settings.lookTarget == name ||
        std::any_of(settings.limbs.begin(), settings.limbs.end(), [name](const FullBodyIkLimbSettings& limb) {
            return limb.target == name || limb.poleTarget == name;
        });
}

FullBodyIkChainBones FullBodyIkChain(const FullBodyIkSettings& settings, std::size_t chain) noexcept {
    if (chain == kFullBodyIkSpineChain) return { settings.spineBoneId, settings.headBoneId };
    return { settings.limbs[chain].upperBoneId, settings.limbs[chain].tipBoneId };
}

Vec3 SolveFullBodyHipsShift(Vec3 initial, std::span<const FullBodyReach> reaches) noexcept {
    Vec3 shift = initial;
    // Moves the hips onto the sphere of the shifts that bring the limb's root within reach of its target.
    const auto settle = [&shift](const FullBodyReach& reach) {
        const Vec3 centre = reach.target - reach.root;
        const Vec3 away = shift - centre;
        const float distance = kb::math::Length(away);
        if (distance <= reach.length) return false;
        shift = centre + away * (reach.length * kReachFraction / distance);
        return true;
    };
    for (std::uint32_t pass = 0U; pass < kMaxPasses; ++pass) {
        bool moved = false;
        for (const FullBodyReach& reach : reaches) {
            if (!reach.priority) moved = settle(reach) || moved;
        }
        for (const FullBodyReach& reach : reaches) {
            if (reach.priority) moved = settle(reach) || moved;
        }
        if (!moved) break;
    }
    for (std::uint32_t pass = 0U; pass < kMaxPasses; ++pass) {
        bool moved = false;
        for (const FullBodyReach& reach : reaches) {
            if (reach.priority) moved = settle(reach) || moved;
        }
        if (!moved) break;
    }
    return shift;
}

} // namespace kb::scene
