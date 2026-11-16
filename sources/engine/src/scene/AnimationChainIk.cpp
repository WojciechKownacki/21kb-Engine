#include "engine/scene/AnimationChainIk.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kb::scene {
namespace {

using kb::math::Quat;
using kb::math::Vec3;

constexpr float kMinSegmentLength = 1.0e-5F;

[[nodiscard]] bool IsFinite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] Vec3 DirectionOr(Vec3 value, Vec3 fallback) noexcept {
    const Vec3 direction = kb::math::Normalize(value);
    return kb::math::Dot(direction, direction) > 0.0F ? direction : fallback;
}

// `value` without its part along the unit `axis`.
[[nodiscard]] Vec3 AcrossAxis(Vec3 value, Vec3 axis) noexcept {
    return value - axis * kb::math::Dot(value, axis);
}

[[nodiscard]] Vec3 AnyPerpendicular(Vec3 axis) noexcept {
    const Vec3 helper = std::abs(axis.y) < 0.9F ? Vec3{ 0.0F, 1.0F, 0.0F } : Vec3{ 1.0F, 0.0F, 0.0F };
    return kb::math::Normalize(kb::math::Cross(axis, helper));
}

// Walks from the fixed root and puts every joint back at its segment length along the direction it has now.
void ImposeLengths(std::span<Vec3> joints, std::span<const float> lengths, Vec3 fallback) {
    for (std::size_t index = 0U; index + 1U < joints.size(); ++index) {
        joints[index + 1U] = joints[index] + DirectionOr(joints[index + 1U] - joints[index], fallback) * lengths[index];
    }
}

// A chain lying straight along the line to a nearer target has no side to fold toward; a slight bow (toward the
// pole when there is one) lets both solvers find the fold.
void BowStraightChain(
    std::span<Vec3> joints, std::span<const float> lengths, Vec3 target, std::optional<Vec3> pole, float total) {
    const Vec3 root = joints.front();
    const Vec3 axis = kb::math::Normalize(target - root);
    float largestOffsetSquared = 0.0F;
    for (std::size_t index = 1U; index + 1U < joints.size(); ++index) {
        const Vec3 offset = AcrossAxis(joints[index] - root, axis);
        largestOffsetSquared = std::max(largestOffsetSquared, kb::math::Dot(offset, offset));
    }
    const float straight = 1.0e-4F * total;
    if (largestOffsetSquared > straight * straight) return;
    const Vec3 poleSide = pole.has_value() ? kb::math::Normalize(AcrossAxis(*pole - root, axis)) : Vec3{};
    const Vec3 bend = kb::math::Dot(poleSide, poleSide) > 0.0F ? poleSide : AnyPerpendicular(axis);
    float along = 0.0F;
    for (std::size_t index = 1U; index + 1U < joints.size(); ++index) {
        along += lengths[index - 1U];
        joints[index] = joints[index] + bend * (0.02F * total * std::sin(kb::math::kPi * along / total));
    }
    ImposeLengths(joints, lengths, axis);
}

void SolveFabrik(std::span<Vec3> joints, std::span<const float> lengths, Vec3 target, const ChainIkSettings& settings) {
    const std::size_t last = joints.size() - 1U;
    const Vec3 root = joints[0];
    const Vec3 fallback = DirectionOr(target - root, Vec3{ 0.0F, 1.0F, 0.0F });
    for (std::uint32_t pass = 0U; pass < settings.maxIterations; ++pass) {
        if (kb::math::Length(joints[last] - target) <= settings.tolerance) return;
        joints[last] = target;
        for (std::size_t index = last; index-- > 0U;) {
            joints[index] = joints[index + 1U] +
                DirectionOr(joints[index] - joints[index + 1U], fallback) * lengths[index];
        }
        joints[0] = root;
        ImposeLengths(joints, lengths, fallback);
    }
}

void SolveCcd(std::span<Vec3> joints, Vec3 target, const ChainIkSettings& settings) {
    const std::size_t last = joints.size() - 1U;
    for (std::uint32_t pass = 0U; pass < settings.maxIterations; ++pass) {
        for (std::size_t pivot = last; pivot-- > 0U;) {
            if (kb::math::Length(joints[last] - target) <= settings.tolerance) return;
            const Vec3 origin = joints[pivot];
            const Quat turn = ShortestArc(joints[last] - origin, target - origin);
            for (std::size_t index = pivot + 1U; index <= last; ++index) {
                joints[index] = origin + kb::math::Rotate(turn, joints[index] - origin);
            }
        }
    }
}

// Turns the joints between the ends about the root-to-effector axis until the most bent of them lies toward the
// pole. A chain with no bend, or a pole on the axis, has no side to choose and stays as it is.
void BendTowardPole(std::span<Vec3> joints, Vec3 pole, float total) {
    const Vec3 root = joints.front();
    const Vec3 axis = kb::math::Normalize(joints.back() - root);
    if (kb::math::Dot(axis, axis) == 0.0F) return;
    Vec3 bent{};
    for (std::size_t index = 1U; index + 1U < joints.size(); ++index) {
        const Vec3 offset = AcrossAxis(joints[index] - root, axis);
        if (kb::math::Dot(offset, offset) > kb::math::Dot(bent, bent)) bent = offset;
    }
    const Vec3 side = AcrossAxis(pole - root, axis);
    const float resolvable = 1.0e-6F * total;
    if (kb::math::Dot(bent, bent) <= resolvable * resolvable || kb::math::Dot(side, side) <= resolvable * resolvable) {
        return;
    }
    const float angle = std::atan2(kb::math::Dot(axis, kb::math::Cross(bent, side)), kb::math::Dot(bent, side));
    const Vec3 spin = axis * std::sin(angle * 0.5F);
    const Quat turn{ spin.x, spin.y, spin.z, std::cos(angle * 0.5F) };
    for (std::size_t index = 1U; index + 1U < joints.size(); ++index) {
        joints[index] = root + kb::math::Rotate(turn, joints[index] - root);
    }
}

} // namespace

Quat ShortestArc(Vec3 from, Vec3 to) noexcept {
    const Vec3 a = kb::math::Normalize(from);
    const Vec3 b = kb::math::Normalize(to);
    if (kb::math::Dot(a, a) == 0.0F || kb::math::Dot(b, b) == 0.0F) return Quat{};
    const float cosine = kb::math::Dot(a, b);
    if (cosine < -0.99999F) {
        const Vec3 axis = AnyPerpendicular(a);
        return Quat{ axis.x, axis.y, axis.z, 0.0F };
    }
    const Vec3 cross = kb::math::Cross(a, b);
    return kb::math::Normalize(Quat{ cross.x, cross.y, cross.z, 1.0F + cosine });
}

void SolveChainIk(
    std::span<Vec3> joints, std::span<float> segmentLengths, Vec3 target, std::optional<Vec3> pole,
    const ChainIkSettings& settings) {
    if (joints.size() < 2U || segmentLengths.size() + 1U < joints.size()) {
        throw std::invalid_argument("Chain IK needs two or more joints and scratch for their segments");
    }
    if (!IsChainIkSettingsValid(settings)) {
        throw std::invalid_argument("Chain IK settings are out of range");
    }
    if (!IsFinite(target) || (pole.has_value() && !IsFinite(*pole)) ||
        !std::all_of(joints.begin(), joints.end(), [](Vec3 joint) { return IsFinite(joint); })) {
        throw std::invalid_argument("Chain IK input is not finite");
    }
    float total = 0.0F;
    for (std::size_t index = 0U; index + 1U < joints.size(); ++index) {
        segmentLengths[index] = kb::math::Length(joints[index + 1U] - joints[index]);
        if (segmentLengths[index] <= kMinSegmentLength) {
            throw std::invalid_argument("Chain IK requires non-zero bone lengths");
        }
        total += segmentLengths[index];
    }
    const std::span<const float> lengths = segmentLengths.first(joints.size() - 1U);
    const Vec3 root = joints.front();
    const Vec3 toTarget = target - root;
    if (kb::math::Length(toTarget) >= total) {
        const Vec3 direction = kb::math::Normalize(toTarget);
        for (std::size_t index = 0U; index + 1U < joints.size(); ++index) {
            joints[index + 1U] = joints[index] + direction * lengths[index];
        }
        return;
    }
    BowStraightChain(joints, lengths, target, pole, total);
    if (settings.solver == ChainIkSolver::Fabrik) {
        SolveFabrik(joints, lengths, target, settings);
    } else {
        SolveCcd(joints, target, settings);
    }
    if (pole.has_value()) BendTowardPole(joints, *pole, total);
}

} // namespace kb::scene
