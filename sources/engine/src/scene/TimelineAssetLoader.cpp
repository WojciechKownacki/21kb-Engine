#include "engine/scene/TimelineAssetLoader.hpp"

#include "engine/assets/AssetMemoryInputStream.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/TimelineAsset.hpp"
#include "engine/scene/TimelineAssetIO.hpp"

#include <memory>
#include <algorithm>

namespace kb::scene {
namespace {
const kb::assets::AssetMetadata* ResolveAudio(const TimelineAudioTrack& track,
    const kb::assets::AssetRegistry& registry) {
    kb::assets::AssetId id;
    const auto* metadata = kb::assets::TryParseAssetId(track.clipReference, id) && id.IsValid()
        ? registry.Find(id) : registry.FindByPath(std::filesystem::path{track.clipReference});
    return metadata != nullptr && ((metadata->type == "AudioClip" && metadata->importCategory.empty()) ||
        (metadata->type == "ImportedAsset" && metadata->importCategory == "Audio")) ? metadata : nullptr;
}

std::optional<std::string> ValidateReferences(const TimelineAsset& asset, const kb::assets::AssetRegistry& registry) {
    for (const auto& track : asset.audioTracks) {
        if (ResolveAudio(track, registry) == nullptr)
            return "Timeline audio clip is missing or has an incompatible type: " + track.clipReference;
    }
    for (const auto& track : asset.sequenceTracks) {
        kb::assets::AssetId id;
        const auto* metadata = kb::assets::TryParseAssetId(track.sequenceReference, id) && id.IsValid()
            ? registry.Find(id) : registry.FindByPath(std::filesystem::path{track.sequenceReference});
        if (metadata == nullptr || metadata->type != kTimelineAssetType)
            return "Timeline sequence is missing or has an incompatible type: " + track.sequenceReference;
    }
    for (const auto& track : asset.animationTracks) {
        kb::assets::AssetId id;
        const auto* metadata = kb::assets::TryParseAssetId(track.clipReference, id) && id.IsValid()
            ? registry.Find(id) : registry.FindByPath(std::filesystem::path{track.clipReference});
        if (metadata == nullptr || metadata->type != "AnimationClip")
            return "Timeline animation clip is missing or has an incompatible type: " + track.clipReference;
    }
    return std::nullopt;
}
} // namespace

std::string_view TimelineAssetLoader::Type() const noexcept {
    return kTimelineAssetType;
}

std::type_index TimelineAssetLoader::PayloadType() const noexcept {
    return typeid(TimelineAsset);
}

std::vector<std::string> TimelineAssetLoader::Extensions() const {
    return { kTimelineAssetExtension };
}

std::vector<kb::assets::AssetId> TimelineAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    const auto asset = TimelineAssetIO::Load(metadata.physicalPath);
    std::vector<kb::assets::AssetId> result;
    if (asset) for (const auto& track : asset->audioTracks) {
        const auto* clip = ResolveAudio(track, registry);
        if (clip != nullptr && std::find(result.begin(), result.end(), clip->id) == result.end())
            result.push_back(clip->id);
    }
    if (asset) for (const auto& track : asset->sequenceTracks) {
        kb::assets::AssetId id;
        const auto* sequence = kb::assets::TryParseAssetId(track.sequenceReference, id) && id.IsValid()
            ? registry.Find(id) : registry.FindByPath(std::filesystem::path{track.sequenceReference});
        if (sequence != nullptr && sequence->type == kTimelineAssetType &&
            std::find(result.begin(), result.end(), sequence->id) == result.end()) result.push_back(sequence->id);
    }
    if (asset) for (const auto& track : asset->animationTracks) {
        kb::assets::AssetId id;
        const auto* clip = kb::assets::TryParseAssetId(track.clipReference, id) && id.IsValid()
            ? registry.Find(id) : registry.FindByPath(std::filesystem::path{track.clipReference});
        if (clip != nullptr && clip->type == "AnimationClip" &&
            std::find(result.begin(), result.end(), clip->id) == result.end()) result.push_back(clip->id);
    }
    return result;
}

std::optional<std::string> TimelineAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    const auto asset = TimelineAssetIO::Load(metadata.physicalPath);
    if (!asset) return "Timeline asset could not be loaded or validated";
    return ValidateReferences(*asset, registry);
}

std::optional<std::string> TimelineAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request, const kb::assets::AssetRegistry& registry) const {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes, error)) return error;
    kb::assets::AssetMemoryInputStream input{bytes};
    const auto asset = TimelineAssetIO::Load(input);
    if (!asset) return "Timeline asset could not be loaded or validated";
    return ValidateReferences(*asset, registry);
}

kb::assets::AssetLoadResult TimelineAssetLoader::Load(
    const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ {}, std::move(error) };
    }
    kb::assets::AssetMemoryInputStream input{ sourceBytes };
    std::optional<TimelineAsset> asset = TimelineAssetIO::Load(input);
    return asset
        ? kb::assets::AssetLoadResult{
              std::make_shared<TimelineAsset>(std::move(*asset)), {} }
        : kb::assets::AssetLoadResult{
              {}, "Timeline asset could not be loaded or validated." };
}

} // namespace kb::scene
