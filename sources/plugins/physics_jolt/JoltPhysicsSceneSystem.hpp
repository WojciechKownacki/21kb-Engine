#pragma once

#include "engine/scene/SceneSystem.hpp"

#include <cstddef>
#include <memory>

namespace kb::physics_jolt {

struct JoltPhysicsSceneSystemSettings {
    float fixedDeltaSeconds = 1.0F / 60.0F;
    float maxFrameDeltaSeconds = 0.25F;
    std::size_t maxFixedStepsPerFrame = 8U;
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
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace kb::physics_jolt
