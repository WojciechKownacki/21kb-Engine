#include "assets/MiniaudioClipResolver.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/ImportedAsset.hpp"
#include "engine/assets/AssetImportCatalog.hpp"
#include "engine/audio/AudioClipFormats.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace kb::audio_miniaudio {
namespace {

[[nodiscard]] std::filesystem::path ResolveImportedAudio(kb::scene::Scene& scene, const kb::assets::AssetMetadata& metadata) {
    const kb::assets::AssetHandle<kb::assets::ImportedAsset> imported = scene.Assets().Manager().Load<kb::assets::ImportedAsset>(metadata.id);
    if (!imported.IsLoaded() || imported->category != kb::assets::AssetImportCategory::Audio || imported->payload.empty()) {
        return {};
    }

    const std::string_view extension = kb::audio::CanonicalAudioClipExtension(imported->sourceExtension);
    if (extension.empty()) {
        return {};
    }
    const std::filesystem::path cacheRoot = std::filesystem::temp_directory_path() / "21kb_audio_miniaudio";
    const std::filesystem::path resolved = cacheRoot / (kb::assets::ToString(metadata.id) + "_" + std::to_string(metadata.contentHash) + std::string{ extension });

    std::error_code error;
    std::filesystem::create_directories(cacheRoot, error);
    if (error) {
        return {};
    }

    if (!std::filesystem::is_regular_file(resolved, error) || error || std::filesystem::file_size(resolved, error) != imported->payload.size() || error) {
        std::ofstream output{ resolved, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            return {};
        }
        output.write(reinterpret_cast<const char*>(imported->payload.data()), static_cast<std::streamsize>(imported->payload.size()));
        if (!output.good()) {
            return {};
        }
    }

    return resolved;
}

[[nodiscard]] std::filesystem::path ResolveMountedAudio(kb::scene::Scene& scene, const kb::assets::AssetMetadata& metadata) {
    std::filesystem::path resolved = metadata.physicalPath;
    if (resolved.empty()) {
        const std::optional<std::filesystem::path> mountedPath = scene.Assets().Manager().Mounts().Resolve(metadata.virtualPath);
        if (!mountedPath.has_value()) {
            return {};
        }
        resolved = *mountedPath;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved, error) || error) {
        return {};
    }
    return resolved;
}

} // namespace

std::filesystem::path MiniaudioClipResolver::Resolve(kb::scene::Scene& scene, std::uint64_t clipAssetId) const {
    if (clipAssetId == 0U) {
        return {};
    }

    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ clipAssetId });
    if (metadata == nullptr) {
        return {};
    }

    const std::filesystem::path authoredPath = metadata->physicalPath.empty()
        ? metadata->virtualPath
        : metadata->physicalPath;
    if (kb::assets::AssetImportCatalog::IsEngineAssetExtension(authoredPath.extension())) {
        if (metadata->type != "ImportedAsset" || metadata->importCategory != "Audio") {
            return {};
        }
        return ResolveImportedAudio(scene, *metadata);
    }

    if (metadata->type != "AudioClip" || !metadata->importCategory.empty()
        || !kb::audio::IsSupportedAudioClipExtension(authoredPath.extension().string())) {
        return {};
    }
    return ResolveMountedAudio(scene, *metadata);
}

} // namespace kb::audio_miniaudio
