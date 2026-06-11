#pragma once

#include "engine/scene/SceneSystem.hpp"

#include <memory>

namespace kb::audio_miniaudio {

class MiniaudioSceneSystem final : public kb::scene::SceneSystem {
public:
    MiniaudioSceneSystem();
    ~MiniaudioSceneSystem() override;

    MiniaudioSceneSystem(const MiniaudioSceneSystem&) = delete;
    MiniaudioSceneSystem& operator=(const MiniaudioSceneSystem&) = delete;
    MiniaudioSceneSystem(MiniaudioSceneSystem&&) noexcept;
    MiniaudioSceneSystem& operator=(MiniaudioSceneSystem&&) noexcept;

    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace kb::audio_miniaudio
