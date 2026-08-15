#include "assets/AssetDiscoveryService.hpp"

#include "assets/AssetFileSystem.hpp"
#include "assets/AssetLoaderRegistry.hpp"
#include "assets/AssetPathUtilities.hpp"
#include "engine/assets/ImportedAssetHeaderReader.hpp"

#include <set>
#include <system_error>
#include <unordered_map>

namespace kb::assets {
namespace {

constexpr const char* kEditorLiveAssetOverrideCategory = "EditorLiveOverride";

[[nodiscard]] bool IsEditorLiveAssetOverride(const AssetMetadata& metadata) noexcept {
    return metadata.importCategory == kEditorLiveAssetOverrideCategory && metadata.runtimeLoadable;
}

} // namespace

std::size_t AssetDiscoveryService::DiscoverMountedAssets(
    const AssetMountTable& mounts,
    AssetRegistry& registry,
    const std::vector<std::unique_ptr<IAssetLoader>>& loaders,
    std::unordered_map<std::uint64_t, AssetManager::CachedAsset>& cache) {
    std::size_t discovered = 0;
    std::set<std::uint64_t> discoveredIds;
    std::set<std::string> discoveredVirtualPaths;
    std::vector<AssetMetadata> previousAssets;
    previousAssets.assign(registry.All().begin(), registry.All().end());
    std::unordered_map<std::uint64_t, AssetMetadata> previousById;
    previousById.reserve(previousAssets.size());
    for (const AssetMetadata& metadata : previousAssets) {
        previousById.emplace(metadata.id.value, metadata);
    }

    for (const AssetMount& mount : mounts.Mounts()) {
        std::error_code error;
        if (!std::filesystem::exists(mount.root, error) || error) {
            continue;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator{ mount.root, error }) {
            if (error || !entry.is_regular_file()) {
                continue;
            }

            IAssetLoader* loader = AssetLoaderRegistry::FindByExtension(loaders, entry.path().extension());
            if (loader == nullptr) {
                continue;
            }

            std::optional<std::filesystem::path> virtualPath = mounts.ToVirtual(entry.path());
            if (!virtualPath.has_value()) {
                continue;
            }

            std::string assetType{ loader->Type() };
            std::string importCategory;
            if (assetType == "ImportedAsset") {
                if (const std::optional<AssetImportCategory> category = ImportedAssetHeaderReader::ReadCategory(entry.path())) {
                    importCategory = std::string{ ToString(*category) };
                    assetType = std::string{ RuntimeAssetType(*category) };
                }
            } else {
                importCategory = loader->DiscoverImportCategory(entry.path());
            }

            const AssetId id = MakeAssetId(NormalizeAssetPath(*virtualPath) + ":" + assetType);
            AssetMetadata metadata{
                .id = id,
                .type = assetType,
                .importCategory = importCategory,
                .name = entry.path().stem().string(),
                .virtualPath = *virtualPath,
                .physicalPath = entry.path(),
                .contentHash = AssetFileSystem::HashFile(entry.path()),
                .dependencies = {},
                .runtimeLoadable = true,
            };
            const auto previous = previousById.find(id.value);
            if (previous != previousById.end() && IsEditorLiveAssetOverride(previous->second)) {
                discoveredVirtualPaths.insert(NormalizeAssetPath(*virtualPath));
                if (registry.Upsert(previous->second)) {
                    ++discovered;
                    discoveredIds.insert(id.value);
                }
                continue;
            }
            if (previous != previousById.end() &&
                (previous->second.contentHash != metadata.contentHash ||
                 previous->second.type != metadata.type ||
                 previous->second.importCategory != metadata.importCategory ||
                 NormalizeAssetPath(previous->second.virtualPath) != NormalizeAssetPath(metadata.virtualPath))) {
                static_cast<void>(cache.erase(id.value));
            }
            discoveredVirtualPaths.insert(NormalizeAssetPath(*virtualPath));
            if (registry.Upsert(std::move(metadata))) {
                ++discovered;
                discoveredIds.insert(id.value);
            }
        }
    }

    std::vector<AssetId> dependencyRefreshIds;
    dependencyRefreshIds.reserve(registry.All().size());
    for (const AssetMetadata& metadata : registry.All()) {
        dependencyRefreshIds.push_back(metadata.id);
    }
    for (const AssetId id : dependencyRefreshIds) {
        AssetMetadata* metadata = registry.FindMutable(id);
        if (metadata == nullptr || metadata->physicalPath.empty()) {
            continue;
        }
        IAssetLoader* loader = AssetLoaderRegistry::FindByExtension(loaders, metadata->physicalPath.extension());
        if (loader == nullptr) {
            metadata->dependencies.clear();
            continue;
        }
        metadata->dependencies = loader->DiscoverDependencies(*metadata, registry);
    }

    for (const AssetMetadata& metadata : previousAssets) {
        const std::string normalizedVirtualPath = NormalizeAssetPath(metadata.virtualPath);
        if (!AssetPathUtilities::IsMountedVirtualPath(mounts, metadata.virtualPath) || discoveredIds.contains(metadata.id.value)) {
            continue;
        }

        const std::filesystem::path physical = AssetPathUtilities::ResolvePhysicalPath(mounts, metadata);
        std::error_code error;
        if (discoveredVirtualPaths.contains(normalizedVirtualPath)
            || physical.empty()
            || !std::filesystem::is_regular_file(physical, error)
            || AssetLoaderRegistry::FindByExtension(loaders, physical.extension()) == nullptr) {
            static_cast<void>(registry.Remove(metadata.id));
            static_cast<void>(cache.erase(metadata.id.value));
        }
    }
    return discovered;
}

std::vector<std::filesystem::path> AssetDiscoveryService::VirtualFolders(
    const AssetMountTable& mounts,
    const AssetRegistry& registry) {
    std::set<std::string> folders;
    for (const AssetMount& mount : mounts.Mounts()) {
        const std::filesystem::path rootVirtual{ "/" + mount.name };
        folders.insert(NormalizeAssetPath(rootVirtual));

        std::error_code error;
        if (!std::filesystem::exists(mount.root, error) || error) {
            continue;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator{ mount.root, error }) {
            if (error || !entry.is_directory()) {
                continue;
            }
            if (std::optional<std::filesystem::path> virtualPath = mounts.ToVirtual(entry.path())) {
                folders.insert(NormalizeAssetPath(*virtualPath));
            }
        }
    }

    for (const AssetMetadata& metadata : registry.All()) {
        std::filesystem::path folder = AssetPathUtilities::ParentVirtualPath(metadata.virtualPath);
        while (!folder.empty()) {
            folders.insert(NormalizeAssetPath(folder));
            const std::filesystem::path parent = AssetPathUtilities::ParentVirtualPath(folder);
            if (parent == folder) {
                break;
            }
            folder = parent;
        }
    }

    std::vector<std::filesystem::path> output;
    output.reserve(folders.size());
    for (const std::string& folder : folders) {
        output.emplace_back(folder);
    }
    return output;
}

} // namespace kb::assets
