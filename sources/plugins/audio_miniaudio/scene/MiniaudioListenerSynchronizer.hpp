#pragma once

#include <miniaudio.h>

namespace kb::scene {

class SceneSystemContext;

} // namespace kb::scene

namespace kb::audio_miniaudio {

class MiniaudioListenerSynchronizer final {
public:
    void Sync(ma_engine& engine, kb::scene::SceneSystemContext& context) const;
};

} // namespace kb::audio_miniaudio
