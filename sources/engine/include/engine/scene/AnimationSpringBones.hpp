#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/SkeletonAsset.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace kb::scene {

enum class SpringColliderShape : std::uint8_t {
    Sphere,
    Capsule,
};

// A sphere or capsule fixed to a bone that the chain cannot enter.
struct SpringBoneCollider {
    SkeletonBoneId boneId = 0U;
    SpringColliderShape shape = SpringColliderShape::Sphere;
    // In the bone's space: the sphere's centre, or one end of the capsule.
    kb::math::Vec3 offset{};
    // Capsule only: the other end.
    kb::math::Vec3 tailOffset{};
    float radius = 0.1F;
};

inline constexpr std::size_t kSpringBoneMaxColliders = 64U;

struct SpringBoneSettings {
    // How hard each joint is pulled back to its animated place, in 1/s^2. Zero leaves the chain loose.
    float stiffness = 100.0F;
    // How fast the swing dies out, in 1/s.
    float damping = 10.0F;
    // Downward acceleration in m/s^2. A chain pulled down rests below its animated pose.
    float gravity = 0.0F;
    // How much the character's own movement swings the chain: 0 none, 1 full.
    float movementInfluence = 1.0F;
    // Added to every collider's radius, in the character's units.
    float collisionRadius = 0.0F;
    // The most a bone may turn away from its animated direction; 180 for no limit.
    float maxAngleDegrees = 180.0F;
    // A move of the character larger than this in one update puts the chain back on the animation at once.
    float teleportDistance = 5.0F;
    std::vector<SpringBoneCollider> colliders;
};

[[nodiscard]] bool IsSpringBoneSettingsValid(const SpringBoneSettings& settings) noexcept;

// The chain is simulated in fixed steps whatever the frame rate; time left over carries to the next update.
inline constexpr double kSpringBoneStepSeconds = 1.0 / 240.0;
// Steps one update may run. Time beyond them is dropped, so a long stall cannot make the next frame longer.
inline constexpr std::uint32_t kSpringBoneMaxSteps = 16U;

// A collider in the chain's frame: the segment a-b swept by `radius` (a sphere when both ends coincide).
struct SpringCapsule {
    kb::math::Vec3 a{};
    kb::math::Vec3 b{};
    float radius = 0.0F;
};

// One chain's simulation, root joint first. Positions are relative to the frame (the character) and follow
// world axes, so the character's movement is felt as inertia. All buffers are sized by PrepareSpringChain.
struct SpringChainState {
    std::vector<kb::math::Vec3> position;
    std::vector<kb::math::Vec3> previous;
    // The animated joints at the last update, to interpolate toward the new ones across the steps.
    std::vector<kb::math::Vec3> target;
    std::vector<float> length;
    kb::math::DVec3 origin{};
    kb::math::Quat rotation{};
    double pendingSeconds = 0.0;
    bool primed = false;
};

// Sizes the buffers for `jointCount` joints (at least two) and leaves the chain unprimed.
void PrepareSpringChain(SpringChainState& chain, std::size_t jointCount);

// Advances the chain by `deltaSeconds` with Verlet integration: every joint but the root is pulled to its animated
// place, held at its bone length from its parent, kept within the angle limit and out of the colliders. The root
// follows the animation. `targets` are the animated joints now, relative to the frame's `origin` along world axes;
// `origin` and `rotation` are the frame's place in the world. The first call, and a move of the frame beyond the
// teleport distance, start the chain at rest on `targets`. The result is `chain.position`.
// A call never allocates. Throws std::invalid_argument for a chain that was not prepared for `targets`, a
// zero-length bone, or non-finite input.
void StepSpringChain(
    SpringChainState& chain, std::span<const kb::math::Vec3> targets, const kb::math::DVec3& origin,
    kb::math::Quat rotation, double deltaSeconds, const SpringBoneSettings& settings,
    std::span<const SpringCapsule> colliders);

} // namespace kb::scene
