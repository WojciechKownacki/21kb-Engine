#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

// What a 2D ray found. Positions are in the plane of the query: X and Y for a side view, X and Z (written as y) for a top-down view.
struct Physics2DHit {
    bool hit = false;
    SceneEntity entity{};
    float x = 0.0F;
    float y = 0.0F;
    float normalX = 0.0F;
    float normalY = 0.0F;
    float distance = 0.0F;
};

// A joint between two bodies of the 2D physics, made from the current poses: the anchors are points of the plane (x, y) in world
// units, and the joint holds the bodies as they are now.
struct Physics2DJointDesc {
    enum class Type : std::int32_t { Distance, Revolute, Prismatic, Weld };
    Type type = Type::Revolute;
    SceneEntity first{};
    SceneEntity second{};            // invalid: the world
    float x = 0.0F, y = 0.0F;        // the anchor (on the first body)
    float x2 = 0.0F, y2 = 0.0F;      // distance: the anchor on the second body
    float axisX = 1.0F, axisY = 0.0F; // prismatic: the line along which the bodies slide
    bool collideConnected = false;
    bool enableLimit = false;
    float lower = 0.0F, upper = 0.0F; // revolute: degrees; prismatic: units; distance: the shortest and longest length
    bool enableMotor = false;
    float motorSpeed = 0.0F;         // revolute: degrees per second; prismatic: units per second
    float maxMotor = 0.0F;           // the largest torque or force of the motor
    float hertz = 0.0F;              // above 0: a spring (weld: the stiffness of the joint)
    float dampingRatio = 0.7F;
    float length = 0.0F;             // distance: the rest length, 0 for the present distance
};

// Provider-independent access to the 2D physics of a scene. The World.Map2D scene system owns the Box2D worlds; scripts reach them
// through the Physics2D functions. Every call that names an entity needs that entity to have a body (or a character) in the
// running simulation, and answers false otherwise.
class ScenePhysics2DRuntime {
public:
    virtual ~ScenePhysics2DRuntime() = default;
    [[nodiscard]] virtual bool SetVelocity(SceneEntity entity, float x, float y) = 0;
    [[nodiscard]] virtual bool GetVelocity(SceneEntity entity, float& x, float& y) const = 0;
    [[nodiscard]] virtual bool AddImpulse(SceneEntity entity, float x, float y) = 0;
    [[nodiscard]] virtual bool AddForce(SceneEntity entity, float x, float y) = 0;
    [[nodiscard]] virtual Physics2DHit Raycast(bool sideView, float originX, float originY, float directionX, float directionY,
        float maxDistance, std::uint32_t layerMask) const = 0;
    [[nodiscard]] virtual std::vector<SceneEntity> OverlapCircle(bool sideView, float x, float y, float radius, std::uint32_t layerMask) const = 0;
    [[nodiscard]] virtual std::vector<SceneEntity> OverlapBox(bool sideView, float x, float y, float halfWidth, float halfHeight,
        std::uint32_t layerMask) const = 0;
    // The velocity a character walks with this step (the along-the-ground part; gravity and jumps are its own).
    [[nodiscard]] virtual bool MoveCharacter(SceneEntity entity, float x, float y) = 0;
    [[nodiscard]] virtual bool JumpCharacter(SceneEntity entity, float speed) = 0;
    [[nodiscard]] virtual bool IsGrounded(SceneEntity entity) const = 0;
    // Joints made while playing last until they are destroyed or one of their bodies goes away. Returns the joint (above 0), or 0
    // with the reason in `error`.
    [[nodiscard]] virtual std::uint32_t CreateJoint(const Physics2DJointDesc& desc, std::string& error) = 0;
    [[nodiscard]] virtual bool DestroyJoint(std::uint32_t joint) = 0;
    // Drives a revolute or prismatic joint; speed 0 with a force holds it.
    [[nodiscard]] virtual bool SetJointMotor(std::uint32_t joint, float speed, float maxForce) = 0;
    [[nodiscard]] virtual bool JointExists(std::uint32_t joint) const = 0;
};

} // namespace kb::scene
