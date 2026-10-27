#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/audio/AudioMetering.hpp"

#include <string>
#include <string_view>

namespace kb::scene { class Scene; }
namespace kb::editor {
struct EditorAudioMixerRuntimeState {
    kb::audio::AudioMeterReport meters;
    std::string previewClip, diagnostic;
    bool active = false, canCapture = false, canPreview = false, previewPlaying = false;
};

// Transient editor controls use the same scene provider as runtime playback.
// Operations are owner-thread-only and never alter authored mixer/scene data.
class EditorAudioMixerRuntime {
public:
    [[nodiscard]] static EditorAudioMixerRuntimeState Query(const kb::scene::Scene& scene, kb::assets::AssetId mixer);
    [[nodiscard]] static bool ConfigureCapture(kb::scene::Scene& scene, kb::assets::AssetId mixer, bool enabled);
    [[nodiscard]] static bool ResetCapture(kb::scene::Scene& scene, kb::assets::AssetId mixer);
    [[nodiscard]] static bool SetPreviewClip(kb::scene::Scene& scene, kb::assets::AssetId mixer, std::string_view clip);
    [[nodiscard]] static bool Play(kb::scene::Scene& scene, kb::assets::AssetId mixer, std::string_view bus);
    static void Stop(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static bool ShouldRefresh(kb::scene::Scene& scene);
};
} // namespace kb::editor
