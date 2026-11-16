#include "engine/scene/AnimationSpringBones.hpp"

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

[[nodiscard]] bool Within(float value, float low, float high) noexcept {
    return value >= low && value <= high;
}

// Moves `point`, which lies `length` from `parent`, out of the capsule along that sphere around the parent, so the
// bone keeps its length. A parent already inside the capsule leaves nowhere to go and the point stays.
[[nodiscard]] Vec3 PushOut(Vec3 point, Vec3 parent, float length, const SpringCapsule& capsule) noexcept {
    // The capsule's nearest point moves with the joint, so a second pass settles it.
    for (int pass = 0; pass < 2; ++pass) {
        const Vec3 span = capsule.b - capsule.a;
        const float spanSquared = kb::math::Dot(span, span);
        const float along = spanSquared > 0.0F
            ? kb::math::Clamp(kb::math::Dot(point - capsule.a, span) / spanSquared, 0.0F, 1.0F)
            : 0.0F;
        const Vec3 centre = capsule.a + span * along;
        if (kb::math::Length(point - centre) >= capsule.radius) return point;
        const Vec3 axis = centre - parent;
        const float distance = kb::math::Length(axis);
        if (distance <= kMinSegmentLength) return point;
        const Vec3 unit = axis * (1.0F / distance);
        // The circle where the sphere around the parent meets the capsule's surface; the joint goes to its
        // nearest point.
        const float reach = (length * length - capsule.radius * capsule.radius + distance * distance) / (2.0F * distance);
        const float ringSquared = length * length - reach * reach;
        if (ringSquared < 0.0F) return point;
        const Vec3 offset = point - parent;
        Vec3 side = kb::math::Normalize(offset - unit * kb::math::Dot(offset, unit));
        if (kb::math::Dot(side, side) == 0.0F) side = AnyPerpendicular(unit);
        point = parent + unit * reach + side * std::sqrt(ringSquared);
    }
    return point;
}

} // namespace

bool IsSpringBoneSettingsValid(const SpringBoneSettings& settings) noexcept {
    constexpr float kLarge = 1.0e6F;
    return Within(settings.stiffness, 0.0F, 10000.0F) && Within(settings.damping, 0.0F, 1000.0F) &&
        Within(settings.gravity, -100.0F, 100.0F) && Within(settings.movementInfluence, 0.0F, 1.0F) &&
        Within(settings.collisionRadius, 0.0F, 100.0F) && Within(settings.maxAngleDegrees, 0.0F, 180.0F) &&
        settings.teleportDistance > 0.0F && settings.teleportDistance <= kLarge &&
        settings.colliders.size() <= kSpringBoneMaxColliders &&
        std::all_of(settings.colliders.begin(), settings.colliders.end(), [](const SpringBoneCollider& collider) {
            return collider.boneId != 0U &&
                (collider.shape == SpringColliderShape::Sphere || collider.shape == SpringColliderShape::Capsule) &&
                Within(collider.offset.x, -kLarge, kLarge) && Within(collider.offset.y, -kLarge, kLarge) &&
                Within(collider.offset.z, -kLarge, kLarge) && Within(collider.tailOffset.x, -kLarge, kLarge) &&
                Within(collider.tailOffset.y, -kLarge, kLarge) && Within(collider.tailOffset.z, -kLarge, kLarge) &&
                collider.radius > 0.0F && collider.radius <= 100.0F;
        });
}

void PrepareSpringChain(SpringChainState& chain, std::size_t jointCount) {
    if (jointCount < 2U) throw std::invalid_argument("A spring chain needs two or more joints");
    chain = SpringChainState{};
    chain.position.resize(jointCount);
    chain.previous.resize(jointCount);
    chain.target.resize(jointCount);
    chain.length.resize(jointCount - 1U);
}

void StepSpringChain(
    SpringChainState& chain, std::span<const Vec3> targets, const kb::math::DVec3& origin, Quat rotation,
    double deltaSeconds, const SpringBoneSettings& settings, std::span<const SpringCapsule> colliders) {
    const std::size_t count = chain.position.size();
    if (count < 2U || targets.size() != count || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0 ||
        !std::all_of(targets.begin(), targets.end(), [](Vec3 joint) { return IsFinite(joint); })) {
        throw std::invalid_argument("Spring chain input is not usable");
    }
    for (std::size_t index = 0U; index + 1U < count; ++index) {
        chain.length[index] = kb::math::Length(targets[index + 1U] - targets[index]);
        if (chain.length[index] <= kMinSegmentLength) {
            throw std::invalid_argument("Spring chain requires non-zero bone lengths");
        }
    }
    const Vec3 shift = kb::math::RelativeTo(origin, chain.origin);
    if (!chain.primed || kb::math::Length(shift) > settings.teleportDistance) {
        std::ranges::copy(targets, chain.position.begin());
        std::ranges::copy(targets, chain.previous.begin());
        std::ranges::copy(targets, chain.target.begin());
        chain.origin = origin;
        chain.rotation = rotation;
        chain.pendingSeconds = 0.0;
        chain.primed = true;
        return;
    }

    // The frame moved: carry the chain over by the share of the movement it should feel. Fully felt, the
    // joints stay where they are in the world; not felt, they move with the frame. The old animated joints are
    // carried the same way so the animation's own motion is what the chain is pulled along by.
    const float influence = settings.movementInfluence;
    const Quat carry = kb::math::Slerp(
        Quat{}, kb::math::Normalize(rotation * kb::math::Inverse(chain.rotation)), 1.0F - influence);
    const Vec3 carriedShift = shift * influence;
    for (std::size_t index = 0U; index < count; ++index) {
        chain.position[index] = kb::math::Rotate(carry, chain.position[index] - carriedShift);
        chain.previous[index] = kb::math::Rotate(carry, chain.previous[index] - carriedShift);
        chain.target[index] = kb::math::Rotate(carry, chain.target[index] - carriedShift);
    }
    chain.origin = origin;
    chain.rotation = rotation;

    const double due = (chain.pendingSeconds + deltaSeconds) / kSpringBoneStepSeconds;
    const std::uint32_t steps = static_cast<std::uint32_t>(std::min(due, static_cast<double>(kSpringBoneMaxSteps)));
    chain.pendingSeconds = due >= static_cast<double>(kSpringBoneMaxSteps)
        ? 0.0
        : (due - static_cast<double>(steps)) * kSpringBoneStepSeconds;

    const float step = static_cast<float>(kSpringBoneStepSeconds);
    const float velocityKept = std::exp(-settings.damping * step);
    const Vec3 gravity{ 0.0F, -settings.gravity, 0.0F };
    const bool limited = settings.maxAngleDegrees < 180.0F;
    const float limitRadians = settings.maxAngleDegrees * kb::math::kPi / 180.0F;
    const float limitCosine = std::cos(limitRadians);
    const float limitSine = std::sin(limitRadians);
    for (std::uint32_t done = 1U; done <= steps; ++done) {
        // The animated joints move from where they were to where they are across the steps of this update.
        const float blend = static_cast<float>(done) / static_cast<float>(steps);
        const auto goal = [&chain, targets, blend](std::size_t index) {
            return chain.target[index] + (targets[index] - chain.target[index]) * blend;
        };
        chain.position[0] = goal(0U);
        chain.previous[0] = chain.position[0];
        for (std::size_t index = 1U; index < count; ++index) {
            const Vec3 here = chain.position[index];
            const Vec3 velocity = (here - chain.previous[index]) * velocityKept;
            chain.previous[index] = here;
            chain.position[index] = here + velocity + ((goal(index) - here) * settings.stiffness + gravity) * (step * step);
        }
        for (std::size_t index = 1U; index < count; ++index) {
            const Vec3 parent = chain.position[index - 1U];
            const Vec3 animated = kb::math::Normalize(goal(index) - goal(index - 1U));
            Vec3 direction = kb::math::Normalize(chain.position[index] - parent);
            if (kb::math::Dot(direction, direction) == 0.0F) direction = animated;
            const float cosine = kb::math::Dot(direction, animated);
            if (limited && cosine < limitCosine) {
                Vec3 side = kb::math::Normalize(direction - animated * cosine);
                if (kb::math::Dot(side, side) == 0.0F) side = AnyPerpendicular(animated);
                direction = animated * limitCosine + side * limitSine;
            }
            Vec3 point = parent + direction * chain.length[index - 1U];
            for (const SpringCapsule& capsule : colliders) {
                point = PushOut(point, parent, chain.length[index - 1U], capsule);
            }
            chain.position[index] = point;
        }
    }
    std::ranges::copy(targets, chain.target.begin());
}

} // namespace kb::scene
