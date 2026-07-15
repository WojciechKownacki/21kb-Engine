#pragma once

#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

// LIB-123: the character's collision capsule - deliberately the same three
// fields as ColliderComponent's capsule case (center/radius/height), not a
// full collider (a character controller is always exactly one shape, never
// box/sphere/composite). This mirrors ColliderComponent's own position
// convention exactly: `center` offsets the capsule's CENTER relative to the
// entity's transform position, not the capsule's bottom.
//
// LIB-131: slopeLimitDegrees/stepOffset/gravityScale/useGravity - the four
// tunable inputs the character-controller backend (JPH::CharacterVirtual,
// see JoltPhysicsSceneSystem.cpp) actually consumes every fixed step.
// slopeLimitDegrees/stepOffset default to Jolt's own documented defaults
// (CharacterBaseSettings::mMaxSlopeAngle = 50deg, ExtendedUpdateSettings::
// mWalkStairsStepUp magnitude = 0.4) so an unconfigured component behaves
// exactly like Jolt's own out-of-the-box character. gravityScale/useGravity
// mirror RigidbodyComponent's identically-named fields - a character has no
// Rigidbody of its own (CharacterVirtual is not a Body), so it needs its own
// copy of that same toggle rather than reading a sibling component that may
// not exist.
struct CharacterControllerComponent {
    Vec3 center{};
    float radius = 0.5F;
    float height = 2.0F;
    float slopeLimitDegrees = 50.0F;
    float stepOffset = 0.4F;
    float gravityScale = 1.0F;
    bool useGravity = true;
};

} // namespace kb::scene
