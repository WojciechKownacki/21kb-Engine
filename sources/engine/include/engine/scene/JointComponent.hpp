#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <array>
#include <cstddef>

namespace kb::scene {

// Joint kinds supported by the 3D physics backend. The value is stored in scene
// files, so kinds are only ever appended.
//
// Slider, Cone and SixDof work in a joint frame: its first axis is `axis`, its
// second is `freedom.secondaryAxis` made perpendicular to the first (any
// perpendicular when the two are parallel). The frame is placed at `anchor` on
// this body and at `connectedAnchor` on the other; both frames coincide in
// world space when the constraint is created, and every limit is measured from
// that pose. Rebuilding the constraint (editing the joint, or recreating a
// body) takes the current pose as the new neutral one.
enum class JointType {
    Fixed,
    Hinge,
    Distance,
    Point,
    // Translation along `axis` only; a prismatic joint for doors and lifts.
    // enableLimit bounds this body's travel to [minLimit, maxLimit] metres from
    // the pose where the anchors coincide (minLimit <= 0 <= maxLimit); `spring`
    // makes that stop soft and `motor` drives the travel (m/s, N).
    Slider,
    // Ball joint with a swing cone about `axis` (swing1Limit and swing2Limit)
    // and, when enableLimit, a twist range [minLimit, maxLimit] degrees about
    // `axis` itself; without enableLimit the twist is free.
    Cone,
    // Every axis of the joint frame is configured separately in `freedom.axes`.
    SixDof,
    // Keeps the anchors between minLimit and maxLimit metres apart: slack while
    // the distance is inside that range (minLimit may stay 0), taut at maxLimit.
    // `spring` makes the stop elastic. enableLimit has no meaning: a rope always
    // has its length.
    Rope,
};

// A hinge motor drives the owning body about the joint axis at targetVelocity
// (degrees/second), with at most `force` torque (Nm). Zero force disables it.
// On a slider or a SixDof translation axis it is metres/second and newtons.
struct JointMotor {
    float targetVelocity = 0.0F;
    float force = 0.0F;
};

// A distance spring uses stiffness `spring` (N/m) and damping `damper` (Ns/m).
// Zero stiffness holds the separation rigidly.
struct JointSpring {
    float spring = 0.0F;
    float damper = 0.0F;
};

// How one degree of freedom of a SixDof joint moves: held at zero, kept between
// minLimit and maxLimit, or not constrained at all.
enum class JointAxisMode {
    Locked,
    Limited,
    Free,
};

// One translation (metres) or rotation (degrees) axis of a SixDof joint, in the
// joint frame. `motor` drives the axis at a target speed (m/s or degrees/s)
// with at most `force` (N or Nm); zero force leaves the axis undriven.
struct JointAxis {
    JointAxisMode mode = JointAxisMode::Locked;
    float minLimit = 0.0F;
    float maxLimit = 0.0F;
    JointMotor motor{};
};

// Indices into JointFreedom::axes: translation along X, Y, Z then rotation
// about X, Y, Z of the joint frame.
inline constexpr std::size_t kJointAxisCount = 6U;

// Joint settings beyond the shared ones, used by Slider, Cone and SixDof.
struct JointFreedom {
    Vec3 secondaryAxis{ 1.0F, 0.0F, 0.0F };
    // Cone: how far `axis` may tilt about the secondary axis, and about the axis
    // perpendicular to both, in degrees (half angles, 0 to 180; 180 leaves the
    // swing free).
    float swing1Limit = 45.0F;
    float swing2Limit = 45.0F;
    std::array<JointAxis, kJointAxisCount> axes{};
};

// Whether a joint of this kind reads JointComponent::freedom.
[[nodiscard]] constexpr bool JointUsesFreedom(JointType type) noexcept {
    return type == JointType::Slider || type == JointType::Cone || type == JointType::SixDof;
}

// Connects this body to another rigid body, or the static world when
// connectedEntity is invalid, at local-space anchors. Limits apply to the
// owner's hinge angle in degrees or the distance between anchors.
struct JointComponent {
    JointType type = JointType::Fixed;
    SceneEntity connectedEntity{};
    Vec3 anchor{};
    Vec3 connectedAnchor{};
    Vec3 axis{ 0.0F, 1.0F, 0.0F };
    float minLimit = 0.0F;
    float maxLimit = 0.0F;
    bool enableLimit = false;
    JointMotor motor{};
    JointSpring spring{};
    // Force (N) above which the joint breaks and its component is removed.
    // Zero means the joint never breaks.
    float breakForce = 0.0F;
    JointFreedom freedom{};
};

} // namespace kb::scene
