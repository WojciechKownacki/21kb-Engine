#include "engine/audio/AudioClipAssetLoader.hpp"

#include "engine/audio/AudioClipAsset.hpp"
#include "engine/audio/AudioClipFormats.hpp"

#include <filesystem>
#include <memory>
#include <utility>

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
    const std::string sourceExtension{
        CanonicalAudioClipExtension(request.SourceExtension())};
    if (request.metadata.type != "AudioClip" || !request.metadata.importCategory.empty()
        || !IsSupportedAudioClipExtension(sourceExtension)) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Audio clip asset could not be opened." };
    }

    if (request.IsPackaged()) {
        std::vector<std::uint8_t> sourceBytes;
        std::string error;
        if (!request.ReadSourceBytes(sourceBytes, error)) {
            return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
        }
        if (sourceBytes.empty()) {
            return kb::assets::AssetLoadResult{ .asset = {}, .error = "Audio clip asset payload is empty." };
        }
        return kb::assets::AssetLoadResult{
            .asset = std::make_shared<AudioClipAsset>(AudioClipAsset{
                .path = {},
                .encodedBytes = std::move(sourceBytes),
                .sourceExtension = sourceExtension,
            }),
            .error = {},
        };
    }

    if (!std::filesystem::is_regular_file(request.resolvedPath)) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Audio clip asset could not be opened." };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<AudioClipAsset>(AudioClipAsset{
            .path = request.resolvedPath,
            .encodedBytes = {},
            .sourceExtension = sourceExtension,
        }),
        .error = {},
    };
}

} // namespace kb::audio
