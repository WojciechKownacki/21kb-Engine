#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace kb::scene {

class Scene;

// LIB-150: one runtime per-bus volume override (Audio.SetBusVolume) - the strongest layer
// in the mixer's volume resolution (authored -> active snapshot/transition -> override),
// upsert-by-name exactly like LIB-140's material parameter overrides.
struct AudioMixerBusVolumeOverride {
    std::string bus;
    float volume = 1.0F;
};

// LIB-150: the active snapshot transition, advanced deterministically with the scene's own
// delta time (never wall clock). `duration <= 0` means "no transition running". While
// running, ActiveSnapshot still reports the FROM state; completion promotes `toSnapshot`
// to the active snapshot and clears the transition.
struct AudioMixerSnapshotTransition {
    std::string toSnapshot;
    float elapsedSeconds = 0.0F;
    float durationSeconds = 0.0F;

    [[nodiscard]] bool IsActive() const noexcept { return durationSeconds > 0.0F; }
    [[nodiscard]] float Progress() const noexcept {
        return durationSeconds <= 0.0F ? 1.0F : (elapsedSeconds >= durationSeconds ? 1.0F : elapsedSeconds / durationSeconds);
    }
};

// LIB-147: the scene-global "active AudioMixer asset + active snapshot" toggle, mirroring
// ScenePostProcessAccess's exact shape (plain fields on SceneState, no per-entity
// component). Storage is a plain kb::assets::AssetId value (0 = none - the backend then
// routes everything to its implicit master output at unity, the pre-mixer behavior) plus
// the active snapshot's NAME (empty = authored bus volumes, no snapshot applied).
//
// kb::scene deliberately stores only the SELECTION; the mixer's actual content
// (kb::audio::AudioMixerAsset - bus routing graph, snapshot volume sets) is resolved
// lazily, every frame, by the audio backend (the miniaudio plugin's bus registry) through
// AssetManager::Load. An unresolvable mixer keeps named routes unavailable; an empty
// route continues to target the implicit master output. Timed snapshot TRANSITIONS are
// LIB-150's own explicitly-named scope; setting the active snapshot here applies its
// volumes from the next audio tick (an immediate switch).
class SceneAudioMixerAccess {
public:
    SceneAudioMixerAccess() = delete;

    static void SetActiveMixer(Scene& scene, std::uint64_t mixerAssetId) noexcept;
    [[nodiscard]] static std::uint64_t ActiveMixer(const Scene& scene) noexcept;
    static void SetActiveSnapshot(Scene& scene, std::string_view snapshotName);
    [[nodiscard]] static const std::string& ActiveSnapshot(const Scene& scene) noexcept;

    // LIB-150: runtime per-bus volume overrides - upsert-by-name / remove-by-name (false
    // when clearing a bus that has no override). Name validation against the mixer asset
    // happens at the script layer (the asset lives engine-side, so unlike material
    // parameters an unknown bus IS an honest error there). Empty names and invalid
    // gains are rejected without changing the existing override state.
    [[nodiscard]] static bool SetBusVolumeOverride(Scene& scene, std::string_view busName, float volume);
    [[nodiscard]] static bool ClearBusVolumeOverride(Scene& scene, std::string_view busName) noexcept;
    // Drops every runtime override and any running transition - called when the ACTIVE
    // MIXER changes (another mixer's bus/snapshot names are unrelated state).
    static void ResetRuntimeMixerState(Scene& scene) noexcept;
    [[nodiscard]] static std::span<const AudioMixerBusVolumeOverride> BusVolumeOverrides(const Scene& scene) noexcept;

    // LIB-150: begins a timed transition from the CURRENT snapshot state to `toSnapshot`
    // ("" = back to the authored volumes). A non-positive duration applies immediately
    // (plain SetActiveSnapshot). Starting a new transition while one runs retargets from
    // the current blended state's source snapshot (the previous transition completes
    // instantly first - deliberate v1 simplification, documented at the script layer).
    // Returns false without changing state when the snapshot token or duration is invalid.
    [[nodiscard]] static bool BeginSnapshotTransition(Scene& scene, std::string_view toSnapshot, float durationSeconds);
    [[nodiscard]] static const AudioMixerSnapshotTransition& SnapshotTransition(const Scene& scene) noexcept;
    // Advances the running transition with the scene's own delta time (called once per
    // audio tick by the backend). Returns true when this call COMPLETED the transition
    // (toSnapshot promoted to active, transition cleared).
    static bool AdvanceSnapshotTransition(Scene& scene, float deltaSeconds);
};

} // namespace kb::scene
