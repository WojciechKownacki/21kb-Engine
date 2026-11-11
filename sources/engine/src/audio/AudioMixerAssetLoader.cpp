#include "engine/audio/AudioMixerAssetLoader.hpp"

#include "engine/assets/AssetPayloadMemory.hpp"
#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace kb::audio {

namespace {

// The decoded payload object plus the capacities of the buffers it owns.
std::uint64_t MixerResidentBytes(const AudioMixerAsset& mixer) {
    using kb::assets::StringBufferBytes;
    std::uint64_t bytes = sizeof(mixer) + mixer.buses.capacity() * sizeof(AudioMixerBus) +
        mixer.snapshots.capacity() * sizeof(AudioMixerSnapshot);
    for (const AudioMixerBus& bus : mixer.buses) {
        bytes += StringBufferBytes(bus.name) + StringBufferBytes(bus.parentBus) + bus.effects.capacity() * sizeof(AudioDspEffect);
        for (const AudioDspEffect& effect : bus.effects) bytes += StringBufferBytes(effect.id);
    }
    for (const AudioMixerSnapshot& snapshot : mixer.snapshots) {
        bytes += StringBufferBytes(snapshot.name) + snapshot.busVolumes.capacity() * sizeof(AudioMixerSnapshotBusVolume);
        for (const AudioMixerSnapshotBusVolume& volume : snapshot.busVolumes) bytes += StringBufferBytes(volume.bus);
    }
    return bytes;
}

} // namespace

std::string_view AudioMixerAssetLoader::Type() const noexcept {
    return kAudioMixerAssetType;
}

std::type_index AudioMixerAssetLoader::PayloadType() const noexcept {
    return typeid(AudioMixerAsset);
}

std::vector<std::string> AudioMixerAssetLoader::Extensions() const {
    return { kAudioMixerAssetExtension };
}

kb::assets::AssetLoadResult AudioMixerAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }
    std::optional<AudioMixerAsset> asset = AudioMixerAssetIO::Load(sourceBytes);
    if (!asset.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Audio mixer asset could not be loaded, parsed, or validated." };
    }
    auto payload = std::make_shared<AudioMixerAsset>(std::move(*asset));
    const std::uint64_t residentBytes = MixerResidentBytes(*payload);
    return kb::assets::AssetLoadResult{
        .asset = std::move(payload),
        .error = {},
        .residentBytes = residentBytes,
    };
}

} // namespace kb::audio
