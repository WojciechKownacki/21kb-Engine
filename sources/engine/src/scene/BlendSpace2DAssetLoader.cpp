#include "engine/scene/BlendSpace2DAssetLoader.hpp"
#include "engine/scene/BlendSpace2DAssetIO.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetRegistry.hpp"

#include <algorithm>
#include <memory>

namespace kb::scene {
namespace {
const kb::assets::AssetMetadata* Resolve(const kb::assets::AssetRegistry& registry, const std::string& reference) {
    kb::assets::AssetId id;
    return kb::assets::TryParseAssetId(reference,id) && id.IsValid()
        ? registry.Find(id) : registry.FindByPath(kb::assets::Utf8Path(reference));
}

std::optional<std::string> Validate(const BlendSpace2DAsset& asset, const kb::assets::AssetRegistry& registry) {
    for (const auto& sample : asset.samples) {
        const auto* metadata = Resolve(registry,sample.clipReference);
        if (metadata == nullptr || metadata->type != kAnimationClipAssetType)
            return "BlendSpace2D clip is missing or has an incompatible type: " + sample.clipReference;
    }
    return std::nullopt;
}
} // namespace

std::string_view BlendSpace2DAssetLoader::Type() const noexcept { return kBlendSpace2DAssetType; }
std::type_index BlendSpace2DAssetLoader::PayloadType() const noexcept { return typeid(BlendSpace2DAsset); }
std::vector<std::string> BlendSpace2DAssetLoader::Extensions() const { return {kBlendSpace2DAssetExtension}; }
kb::assets::AssetLoadResult BlendSpace2DAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes,error)) return {{},std::move(error)};
    auto asset = BlendSpace2DAssetIO::Load(bytes,error);
    return asset ? kb::assets::AssetLoadResult{std::make_shared<BlendSpace2DAsset>(std::move(*asset)),{}}
                 : kb::assets::AssetLoadResult{{},std::move(error)};
}

std::vector<kb::assets::AssetId> BlendSpace2DAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    std::string error;
    const auto asset = BlendSpace2DAssetIO::Load(metadata.physicalPath,error);
    std::vector<kb::assets::AssetId> result;
    if (asset) for (const auto& sample : asset->samples) {
        const auto* clip = Resolve(registry,sample.clipReference);
        if (clip != nullptr && clip->type == kAnimationClipAssetType &&
            std::ranges::find(result,clip->id) == result.end()) result.push_back(clip->id);
    }
    return result;
}

std::optional<std::string> BlendSpace2DAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    std::string error;
    const auto asset = BlendSpace2DAssetIO::Load(metadata.physicalPath,error);
    return asset ? Validate(*asset,registry) : std::optional<std::string>{std::move(error)};
}

std::optional<std::string> BlendSpace2DAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request, const kb::assets::AssetRegistry& registry) const {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes,error)) return error;
    const auto asset = BlendSpace2DAssetIO::Load(bytes,error);
    return asset ? Validate(*asset,registry) : std::optional<std::string>{std::move(error)};
}
} // namespace kb::scene
