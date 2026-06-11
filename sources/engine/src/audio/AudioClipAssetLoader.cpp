#include "engine/audio/AudioClipAssetLoader.hpp"

#include "engine/audio/AudioClipAsset.hpp"

#include <filesystem>
#include <memory>

namespace kb::audio {

std::string_view AudioClipAssetLoader::Type() const noexcept {
    return "AudioClip";
}

std::type_index AudioClipAssetLoader::PayloadType() const noexcept {
    return typeid(AudioClipAsset);
}

std::vector<std::string> AudioClipAssetLoader::Extensions() const {
    return {
        ".wav",
        ".mp3",
        ".ogg",
        ".flac",
        ".aac",
        ".m4a",
        ".wma",
        ".aiff",
        ".aif",
        ".xm",
        ".mod",
        ".s3m",
        ".it",
        ".mid",
        ".midi",
    };
}

kb::assets::AssetLoadResult AudioClipAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    if (!std::filesystem::is_regular_file(request.resolvedPath)) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Audio clip asset could not be opened." };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<AudioClipAsset>(AudioClipAsset{ .path = request.resolvedPath }),
        .error = {},
    };
}

} // namespace kb::audio
