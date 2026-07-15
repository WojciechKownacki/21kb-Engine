#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

// LIB-130 owns which of these types are actually simulated ("z limitem
// dostepnych, faktycznie obslugiwanych typow") - this enum names the full
// intended contract surface up front so a joint authored today does not need
// its type value migrated later.
enum class JointType {
    Fixed,
    Hinge,
    Distance,
    Point,
};

// LIB-123: connects this entity to another rigid body (or, when
// connectedEntity is invalid, to the static world) at a pair of local-space
// anchors. minLimit/maxLimit/enableLimit are interpreted per JointType (e.g.
// Hinge: swing angle in degrees; Distance: min/max separation) - actually
// constraining simulated bodies from this data is LIB-130's scope, not this
// component's.
struct JointComponent {
    JointType type = JointType::Fixed;
    SceneEntity connectedEntity{};
    Vec3 anchor{};
    Vec3 connectedAnchor{};
    Vec3 axis{ 0.0F, 1.0F, 0.0F };
    float minLimit = 0.0F;
    float maxLimit = 0.0F;
    bool enableLimit = false;
};

} // namespace kb::scene
