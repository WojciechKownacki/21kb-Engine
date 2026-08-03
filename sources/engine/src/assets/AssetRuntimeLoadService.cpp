#include "assets/AssetRuntimeLoadService.hpp"

#include "assets/AssetLoaderRegistry.hpp"
#include "assets/AssetPathUtilities.hpp"

#include <optional>

namespace kb::assets {

std::shared_ptr<void> AssetRuntimeLoadService::LoadUntyped(
    AssetId id,
    std::type_index expectedType,
    const AssetRegistry& registry,
    const AssetMountTable& mounts,
    const std::vector<std::unique_ptr<IAssetLoader>>& loaders,
    std::unordered_map<std::uint64_t, AssetManager::CachedAsset>& cache,
    std::mutex& loaderExecutionMutex,
    std::string& errorMessage) {
    errorMessage.clear();
    if (!id.IsValid()) {
        errorMessage = "Invalid asset id";
        return {};
    }

    // LIB-158: a cache entry may exist but have already released its payload
    // (ReleaseWhenUnreferenced, last handle dropped) — its `weak` is then
    // expired and we must reload, preserving the entry's chosen policy.
    AssetUnloadPolicy policy = AssetUnloadPolicy::Retain;
    const auto cached = cache.find(id.value);
    if (cached != cache.end()) {
        if (cached->second.type != expectedType) {
            errorMessage = "Cached asset payload type mismatch";
            return {};
        }
        if (std::shared_ptr<void> alive = cached->second.weak.lock(); alive != nullptr) {
            return alive;
        }
        policy = cached->second.policy;
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
    if (const std::optional<std::string> diagnostic = loader->ValidateDependencies(*metadata, registry);
        diagnostic.has_value()) {
        errorMessage = "Asset dependency validation failed: " + *diagnostic;
        return {};
    }

    const std::filesystem::path resolvedPath = AssetPathUtilities::ResolvePhysicalPath(mounts, *metadata);
    if (resolvedPath.empty()) {
        errorMessage = "Asset path could not be resolved: " + NormalizeAssetPath(metadata->virtualPath);
        return {};
    }

    AssetLoadResult result;
    {
        std::scoped_lock lock{ loaderExecutionMutex };
        result = loader->Load(AssetLoadRequest{ .metadata = *metadata, .resolvedPath = resolvedPath });
    }
    if (!result.Succeeded()) {
        errorMessage = result.error.empty() ? "Asset loader failed" : std::move(result.error);
        return {};
    }

    cache[id.value] = AssetManager::CachedAsset{
        .retained = policy == AssetUnloadPolicy::Retain ? result.asset : std::shared_ptr<void>{},
        .weak = result.asset,
        .type = expectedType,
        .policy = policy,
    };
    return result.asset;
}

} // namespace kb::assets
