#pragma once

#include "engine/scene/SceneSystem.hpp"

#include <cstddef>
#include <memory>

namespace kb::physics_jolt {

struct JoltPhysicsSceneSystemSettings {
    int collisionSteps = 1;
};

class JoltPhysicsSceneSystem final : public kb::scene::SceneSystem {
public:
    JoltPhysicsSceneSystem();
    explicit JoltPhysicsSceneSystem(JoltPhysicsSceneSystemSettings settings);
    ~JoltPhysicsSceneSystem() override;

    JoltPhysicsSceneSystem(const JoltPhysicsSceneSystem&) = delete;
    JoltPhysicsSceneSystem& operator=(const JoltPhysicsSceneSystem&) = delete;
    JoltPhysicsSceneSystem(JoltPhysicsSceneSystem&&) noexcept;
    JoltPhysicsSceneSystem& operator=(JoltPhysicsSceneSystem&&) noexcept;

    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace kb::physics_jolt
