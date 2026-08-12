#pragma once

#include "engine/input/InputLocalUser.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <miniaudio.h>

namespace kb::scene {

class SceneSystemContext;

} // namespace kb::scene

namespace kb::audio_miniaudio {

class MiniaudioListenerSynchronizer final {
public:
    struct State {
        bool active = false;
        kb::scene::Vec3 position{};
    };

    [[nodiscard]] State Sync(ma_engine& engine, kb::scene::SceneSystemContext& context);
    void Disable(ma_engine& engine) noexcept;
    void Reset() noexcept;

private:
    kb::scene::SceneEntity previousEntity_{};
    kb::input::LocalUserId previousLocalUser_ = kb::input::kPrimaryLocalUser;
    kb::scene::Vec3 previousPosition_{};
    bool hasPreviousPosition_ = false;
};

} // namespace kb::audio_miniaudio
