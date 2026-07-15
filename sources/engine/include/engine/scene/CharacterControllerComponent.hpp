#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

// LIB-123: the character's collision capsule - deliberately the same three
// fields as ColliderComponent's capsule case (center/radius/height), not a
// full collider (a character controller is always exactly one shape, never
// box/sphere/composite). Movement/slope-limit/step-offset/grounding/gravity
// are LIB-131's scope, not this component's - this is the shape contract a
// future character-controller backend needs, matching how RigidbodyComponent
// shipped without force/impulse fields before LIB-124 added them.
struct CharacterControllerComponent {
    Vec3 center{};
    float radius = 0.5F;
    float height = 2.0F;
};

} // namespace kb::scene
