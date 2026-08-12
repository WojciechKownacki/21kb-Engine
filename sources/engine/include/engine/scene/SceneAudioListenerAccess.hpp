#pragma once

#include "engine/input/InputLocalUser.hpp"

namespace kb::scene {

class Scene;

// Selects which local user's authored AudioListenerComponent instances participate in
// runtime listener selection. This is transient playback state: scene documents persist
// each listener's binding, but never this current runtime selection.
class SceneAudioListenerAccess final {
public:
    SceneAudioListenerAccess() = delete;

    static void SetLocalUser(Scene& scene, kb::input::LocalUserId localUser) noexcept;
    [[nodiscard]] static kb::input::LocalUserId LocalUser(const Scene& scene) noexcept;
};

} // namespace kb::scene
