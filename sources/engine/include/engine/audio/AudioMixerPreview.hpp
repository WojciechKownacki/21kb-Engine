#pragma once

namespace kb::scene { class Scene; }
namespace kb::audio {

// Separate optional capability: existing playback and meter vtables stay intact.
class IAudioMixerPreviewBackend {
public:
    virtual ~IAudioMixerPreviewBackend() = default;
    [[nodiscard]] virtual bool SynchronizeMixerPreview(kb::scene::Scene& scene) = 0;
};

class AudioMixerPreview {
public:
    [[nodiscard]] static bool IsSupported(kb::scene::Scene& scene) noexcept;
    // Commit authored mixer edits on the scene owner thread, without advancing
    // gameplay, scripts, physics, the audio clock or meter history.
    [[nodiscard]] static bool Synchronize(kb::scene::Scene& scene);
};
} // namespace kb::audio
