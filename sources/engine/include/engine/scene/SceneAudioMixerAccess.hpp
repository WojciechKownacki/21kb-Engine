#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::scene {

class Scene;

// LIB-147: the scene-global "active AudioMixer asset + active snapshot" toggle, mirroring
// ScenePostProcessAccess's exact shape (plain fields on SceneState, no per-entity
// component). Storage is a plain kb::assets::AssetId value (0 = none - the backend then
// routes everything to its implicit master output at unity, the pre-mixer behavior) plus
// the active snapshot's NAME (empty = authored bus volumes, no snapshot applied).
//
// kb::scene deliberately stores only the SELECTION; the mixer's actual content
// (kb::audio::AudioMixerAsset - bus routing graph, snapshot volume sets) is resolved
// lazily, every frame, by the audio backend (the miniaudio plugin's bus registry) through
// AssetManager::Load - an unresolvable mixer id or an unknown snapshot name honestly falls
// back to "no mixer"/"no snapshot" instead of crashing, the same convention every other
// renderer/audio-consumed asset reference already follows. Timed snapshot TRANSITIONS are
// LIB-150's own explicitly-named scope; setting the active snapshot here applies its
// volumes from the next audio tick (an immediate switch).
class SceneAudioMixerAccess {
public:
    SceneAudioMixerAccess() = delete;

    static void SetActiveMixer(Scene& scene, std::uint64_t mixerAssetId) noexcept;
    [[nodiscard]] static std::uint64_t ActiveMixer(const Scene& scene) noexcept;
    static void SetActiveSnapshot(Scene& scene, std::string_view snapshotName);
    [[nodiscard]] static const std::string& ActiveSnapshot(const Scene& scene) noexcept;
};

} // namespace kb::scene
