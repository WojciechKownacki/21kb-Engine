#pragma once

#include "engine/audio/AudioMixerAsset.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace kb::audio {

// LIB-147: a flat one-record-per-line text format (`.kbmixer`) with a required
// header on newly saved assets, `#` comments, forward-compatible unknown records,
// and atomic save. Records:
//   kbmixer 1 (routing only) or kbmixer 2 (ordered effects)
//   bus <name> <parentBus|-> <volume> <mute 0|1>
//   snapshot <name>
//   snapshotVolume <snapshotName> <busName> <volume>
//   effect <busName> <stableId> <kind> <bypass 0|1> <kind-specific parameters>
// Effects follow their bus declaration. Version 2 is required for effect records;
// routing-only saves retain version 1. Unknown kinds or malformed effect records fail.
// Names are single whitespace-free tokens ("-" marks "no parent" = implicit master).
// Load also accepts legacy non-empty assets without the header. It validates the parsed
// asset through ValidateAudioMixerAsset and honestly fails on a broken routing graph;
// Save refuses to write an invalid asset for the same reason.
class AudioMixerAssetIO final {
public:
    AudioMixerAssetIO() = delete;

    [[nodiscard]] static std::optional<AudioMixerAsset> Load(const std::filesystem::path& path);
    [[nodiscard]] static std::optional<AudioMixerAsset> Load(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const AudioMixerAsset& asset);
};

} // namespace kb::audio
