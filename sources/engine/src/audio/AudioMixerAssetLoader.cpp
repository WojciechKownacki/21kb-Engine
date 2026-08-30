#include "engine/audio/AudioMixerAssetLoader.hpp"

#include "engine/audio/AudioMixerAsset.hpp"
#include "engine/audio/AudioMixerAssetIO.hpp"

#include <memory>
#include <utility>

namespace kb::audio {

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
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<AudioMixerAsset>(std::move(*asset)),
        .error = {},
    };
}

} // namespace kb::audio
