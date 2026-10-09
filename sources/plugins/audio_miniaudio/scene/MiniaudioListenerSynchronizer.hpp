#pragma once

#include "engine/input/InputLocalUser.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <miniaudio.h>

namespace kb::scene {

class SceneSystemContext;

} // namespace kb::scene

namespace kb::audio_miniaudio {

class MiniaudioAudioSpace;

class MiniaudioListenerSynchronizer final {
public:
    struct State {
        bool active = false;
        // In the audio space (world space without one).
        kb::scene::Vec3 position{};
        // How positions kept from earlier ticks move: the audio origin followed the listener (zero otherwise).
        kb::scene::Vec3 shift{};
    };

    // With an audio space the listener moves its origin (MiniaudioAudioSpace::Follow) and is placed relative to it.
    [[nodiscard]] State Sync(ma_engine& engine, kb::scene::SceneSystemContext& context, MiniaudioAudioSpace* space = nullptr);
    void Disable(ma_engine& engine) noexcept;
    void Reset() noexcept;

private:
    kb::scene::SceneEntity previousEntity_{};
    kb::input::LocalUserId previousLocalUser_ = kb::input::kPrimaryLocalUser;
    kb::scene::Vec3 previousPosition_{};
    bool hasPreviousPosition_ = false;
};

} // namespace kb::audio_miniaudio
