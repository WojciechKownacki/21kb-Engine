#pragma once

#include "engine/scene/SceneSystem.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace kb::physics_jolt {

struct JoltPhysicsSceneSystemSettings {
    int collisionSteps = 1;
    // Zero selects a bounded automatic pool; explicit values tune large simulations.
    std::uint32_t workerThreadCount = 0U;
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
    void OnFixedStepBegin(kb::scene::SceneSystemContext& context) override;
    void OnUpdateEnd(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
    [[nodiscard]] bool RequiresFixedStep() const override {
        return true;
    }

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace kb::physics_jolt
