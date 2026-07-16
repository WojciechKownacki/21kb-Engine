#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kb::audio {

inline constexpr const char* kAudioMixerAssetExtension = ".kbmixer";
inline constexpr const char* kAudioMixerAssetType = "AudioMixer";

// LIB-147: one authored routing bus. `parentBus` names another authored bus this one
// feeds into; empty means it feeds the implicit master output directly (the master bus is
// NOT authored - it always exists as the backend engine's endpoint, exactly like
// miniaudio's own ma_engine endpoint, so an empty mixer still plays everything at unity).
// `volume` is a linear gain, `mute` silences the whole subtree. Bus names are single
// whitespace-free tokens - a constraint of the flat one-line-per-record text format below,
// enforced by validation at save/load time rather than silently mangled.
struct AudioMixerBus {
    std::string name;
    std::string parentBus;
    float volume = 1.0F;
    bool mute = false;
};

// LIB-147: one named per-bus volume override inside a snapshot.
struct AudioMixerSnapshotBusVolume {
    std::string bus;
    float volume = 1.0F;
};

// LIB-147: a named parameter set - when active, each listed bus's volume replaces its
// authored value (buses not listed keep their authored volume). This is deliberately the
// whole v1 snapshot payload: mute/pitch/effect parameters and timed TRANSITIONS between
// snapshots are LIB-150's own explicitly-named scope ("snapshot transition"), not smuggled
// in here.
struct AudioMixerSnapshot {
    std::string name;
    std::vector<AudioMixerSnapshotBusVolume> busVolumes;
};

// LIB-147: the authored, serializable audio mixer - the single source of truth for bus
// routing and snapshot parameter sets. Consumed each frame by the audio backend (the
// miniaudio plugin builds one ma_sound_group per bus and applies the active snapshot's
// volumes), selected per scene through kb::scene::SceneAudioMixerAccess, referenced by
// AudioSourceComponent::outputBus / AudioPlayDesc::outputBus by bus name.
struct AudioMixerAsset {
    std::vector<AudioMixerBus> buses;
    std::vector<AudioMixerSnapshot> snapshots;

    [[nodiscard]] const AudioMixerBus* FindBus(std::string_view busName) const noexcept {
        for (const AudioMixerBus& bus : buses) {
            if (bus.name == busName) {
                return &bus;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const AudioMixerSnapshot* FindSnapshot(std::string_view snapshotName) const noexcept {
        for (const AudioMixerSnapshot& snapshot : snapshots) {
            if (snapshot.name == snapshotName) {
                return &snapshot;
            }
        }
        return nullptr;
    }
};

// LIB-147: structural validation shared by save, load, and tests. Returns an empty string
// for a valid asset, otherwise a human-readable description of the FIRST problem found:
// empty/whitespace/duplicate bus or snapshot names, a parentBus naming no authored bus, a
// parent cycle, or a snapshot override naming no authored bus. Load honestly fails on any
// of these (a mixer with a broken routing graph must never reach the backend), unlike
// unknown KEYWORDS in the text format, which stay forward-compatible-ignored.
[[nodiscard]] std::string ValidateAudioMixerAsset(const AudioMixerAsset& asset);

} // namespace kb::audio
