#include "assets/AssetRuntimeLoadService.hpp"

#include "assets/AssetLoaderRegistry.hpp"
#include "assets/AssetPathUtilities.hpp"

namespace kb::assets {

std::shared_ptr<void> AssetRuntimeLoadService::LoadUntyped(
    AssetId id,
    std::type_index expectedType,
    const AssetRegistry& registry,
    const AssetMountTable& mounts,
    const std::vector<std::unique_ptr<IAssetLoader>>& loaders,
    std::unordered_map<std::uint64_t, AssetManager::CachedAsset>& cache,
    std::string& errorMessage) {
    errorMessage.clear();
    if (!id.IsValid()) {
        errorMessage = "Invalid asset id";
        return {};
    }

    const auto cached = cache.find(id.value);
    if (cached != cache.end()) {
        if (cached->second.type != expectedType) {
            errorMessage = "Cached asset payload type mismatch";
            return {};
        }
        return cached->second.payload;
    }

    const AssetMetadata* metadata = registry.Find(id);
    if (metadata == nullptr) {
        errorMessage = "Asset is not registered";
        return {};
    }
    if (!metadata->runtimeLoadable) {
        errorMessage = "Asset is not runtime loadable";
        return {};
    }

    IAssetLoader* loader = AssetLoaderRegistry::FindByType(loaders, metadata->type);
    if (loader == nullptr) {
        errorMessage = "No loader registered for asset type: " + metadata->type;
        return {};
    }
    if (loader->PayloadType() != expectedType) {
        errorMessage = "Requested payload type does not match asset loader";
        return {};
    }

    const std::filesystem::path resolvedPath = AssetPathUtilities::ResolvePhysicalPath(mounts, *metadata);
    if (resolvedPath.empty()) {
        errorMessage = "Asset path could not be resolved: " + NormalizeAssetPath(metadata->virtualPath);
        return {};
    }

    AssetLoadResult result = loader->Load(AssetLoadRequest{ .metadata = *metadata, .resolvedPath = resolvedPath });
    if (!result.Succeeded()) {
        errorMessage = result.error.empty() ? "Asset loader failed" : std::move(result.error);
        return {};
    }

    cache.emplace(id.value, AssetManager::CachedAsset{ .payload = result.asset, .type = expectedType });
    return result.asset;
}

} // namespace kb::assets
