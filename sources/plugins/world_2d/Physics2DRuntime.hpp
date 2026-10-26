#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/ScenePhysics2DRuntime.hpp"
#include "engine/scene/SceneSystem.hpp"

#include <memory>

namespace kb::world2d {

// The 2D physics of a scene, on Box2D: one world for the X and Y plane (side view, gravity down) and one for the X and Z plane (top
// down, no gravity). While playing, every Body 2D, Collider 2D and Character Controller 2D is made into Box2D bodies and shapes,
// stepped on the fixed step, and written back to the transforms; contacts and triggers reach scripts as the same OnCollision and
// OnTrigger events the 3D physics sends. The collision of every Tilemap with an enabled Tilemap Collider is also made into static
// shapes of the plane of its map (a wall map is a side view, a flat map is top down): solid cells, slopes and polygons, ledges that
// can be jumped up through, and trigger cells; they follow the cells of the map when it is edited while playing. Nothing is simulated
// while editing.
class Physics2DRuntime final : public kb::scene::SceneSystem, public kb::scene::ScenePhysics2DRuntime {
public:
    Physics2DRuntime();
    ~Physics2DRuntime() override;
    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
    [[nodiscard]] bool RequiresFixedStep() const override { return true; }

    [[nodiscard]] bool SetVelocity(kb::scene::SceneEntity entity, float x, float y) override;
    [[nodiscard]] bool GetVelocity(kb::scene::SceneEntity entity, float& x, float& y) const override;
    [[nodiscard]] bool AddImpulse(kb::scene::SceneEntity entity, float x, float y) override;
    [[nodiscard]] bool AddForce(kb::scene::SceneEntity entity, float x, float y) override;
    [[nodiscard]] kb::scene::Physics2DHit Raycast(bool sideView, float originX, float originY, float directionX, float directionY,
        float maxDistance, std::uint32_t layerMask) const override;
    [[nodiscard]] std::vector<kb::scene::SceneEntity> OverlapCircle(bool sideView, float x, float y, float radius, std::uint32_t layerMask) const override;
    [[nodiscard]] std::vector<kb::scene::SceneEntity> OverlapBox(bool sideView, float x, float y, float halfWidth, float halfHeight,
        std::uint32_t layerMask) const override;
    [[nodiscard]] bool MoveCharacter(kb::scene::SceneEntity entity, float x, float y) override;
    [[nodiscard]] bool JumpCharacter(kb::scene::SceneEntity entity, float speed) override;
    [[nodiscard]] bool IsGrounded(kb::scene::SceneEntity entity) const override;
    [[nodiscard]] std::uint32_t CreateJoint(const kb::scene::Physics2DJointDesc& desc, std::string& error) override;
    [[nodiscard]] bool DestroyJoint(std::uint32_t joint) override;
    [[nodiscard]] bool SetJointMotor(std::uint32_t joint, float speed, float maxForce) override;
    [[nodiscard]] bool JointExists(std::uint32_t joint) const override;
    [[nodiscard]] std::size_t JointCount() const noexcept;

    // What the simulation holds now, for tests and diagnostics.
    [[nodiscard]] std::size_t BodyCount() const noexcept;
    [[nodiscard]] std::size_t ShapeCount() const noexcept;
    [[nodiscard]] std::size_t CharacterCount() const noexcept;
    // Shapes made from tilemap collision, and the tilemaps they come from.
    [[nodiscard]] std::size_t MapShapeCount() const noexcept;
    [[nodiscard]] std::size_t MapCount() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kb::world2d
