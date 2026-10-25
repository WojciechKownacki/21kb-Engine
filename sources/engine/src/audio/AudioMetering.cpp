#include "engine/audio/AudioMetering.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <cassert>
#if !defined(NDEBUG)
#include <thread>
#endif

namespace kb::audio {
namespace {
IAudioPlaybackBackend* Playback(kb::scene::Scene& scene) noexcept {
    auto& state = kb::scene::SceneAccess::State(scene);
#if !defined(NDEBUG)
    assert(state.audioPlaybackOwnerThread == std::this_thread::get_id() && "audio meters require the scene owner thread");
#endif
    return state.audioPlaybackBackend;
}
} // namespace

bool AudioMetering::Configure(kb::scene::Scene& scene, bool enabled) {
    auto* capability = dynamic_cast<IAudioMeteringBackend*>(Playback(scene));
    return capability != nullptr && capability->ConfigureMeterCapture(scene, enabled);
}

bool AudioMetering::Reset(kb::scene::Scene& scene) {
    auto* capability = dynamic_cast<IAudioMeteringBackend*>(Playback(scene));
    return capability != nullptr && capability->ResetMeterCapture(scene);
}

AudioMeterReport AudioMetering::Report(kb::scene::Scene& scene) {
    auto* backend = Playback(scene);
    auto* capability = dynamic_cast<IAudioMeteringBackend*>(backend);
    if (capability == nullptr) return {.backendAvailable = backend != nullptr};
    return capability->MeterReport(scene);
}
} // namespace kb::audio
