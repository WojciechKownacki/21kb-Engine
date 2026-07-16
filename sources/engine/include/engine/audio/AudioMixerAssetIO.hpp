#pragma once

#include "engine/audio/AudioMixerAsset.hpp"

#include <filesystem>
#include <optional>

namespace kb::audio {

// LIB-147: a flat one-record-per-line text format (`.kbmixer`), mirroring
// ParticleEffectAssetIO's `.kbvfx` convention exactly - `#` comments, unknown keywords
// ignored (forward compatible), atomic save. Records:
//   bus <name> <parentBus|-> <volume> <mute 0|1>
//   snapshot <name>
//   snapshotVolume <snapshotName> <busName> <volume>
// Names are single whitespace-free tokens ("-" marks "no parent" = implicit master).
// Load validates the parsed asset through ValidateAudioMixerAsset and honestly fails on a
// broken routing graph; Save refuses to write an invalid asset for the same reason.
class AudioMixerAssetIO final {
public:
    AudioMixerAssetIO() = delete;

    [[nodiscard]] static std::optional<AudioMixerAsset> Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const AudioMixerAsset& asset);
};

} // namespace kb::audio
