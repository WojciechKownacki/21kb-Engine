#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

// Joint kinds supported by the 3D physics backend.
enum class JointType {
    Fixed,
    Hinge,
    Distance,
    Point,
};

// A hinge motor drives the owning body about the joint axis at targetVelocity
// (degrees/second), with at most `force` torque (Nm). Zero force disables it.
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
};

} // namespace kb::scene
