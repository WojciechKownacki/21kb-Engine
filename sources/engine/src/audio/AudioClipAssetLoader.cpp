#include "engine/audio/AudioClipAssetLoader.hpp"

#include "engine/audio/AudioClipAsset.hpp"
#include "engine/audio/AudioClipFormats.hpp"

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
    std::vector<std::string> extensions;
    extensions.reserve(kSupportedAudioClipExtensions.size());
    for (const std::string_view extension : kSupportedAudioClipExtensions) {
        extensions.emplace_back(extension);
    }
    return extensions;
}

kb::assets::AssetLoadResult AudioClipAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    if (request.metadata.type != "AudioClip" || !request.metadata.importCategory.empty()
        || !IsSupportedAudioClipExtension(request.resolvedPath.extension().string())
        || !std::filesystem::is_regular_file(request.resolvedPath)) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Audio clip asset could not be opened." };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<AudioClipAsset>(AudioClipAsset{ .path = request.resolvedPath }),
        .error = {},
    };
}

} // namespace kb::audio
