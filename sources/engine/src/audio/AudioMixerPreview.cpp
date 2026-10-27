#include "engine/audio/AudioMixerPreview.hpp"
#include "engine/audio/AudioPlayback.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <cassert>
#if !defined(NDEBUG)
#include <thread>
#endif

namespace kb::audio {
namespace {
IAudioMixerPreviewBackend* Find(kb::scene::Scene& scene) noexcept {
    auto& state = kb::scene::SceneAccess::State(scene);
#if !defined(NDEBUG)
    assert(state.audioPlaybackOwnerThread == std::this_thread::get_id() && "audio preview requires the scene owner thread");
#endif
    return dynamic_cast<IAudioMixerPreviewBackend*>(state.audioPlaybackBackend);
}
} // namespace

bool AudioMixerPreview::IsSupported(kb::scene::Scene& scene) noexcept { return Find(scene) != nullptr; }

bool AudioMixerPreview::Synchronize(kb::scene::Scene& scene) {
    auto* backend = Find(scene);
    return backend != nullptr && backend->SynchronizeMixerPreview(scene);
}
} // namespace kb::audio
