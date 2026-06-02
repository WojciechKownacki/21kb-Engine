#include "assets/AssetDiscoveryService.hpp"

#include "assets/AssetFileSystem.hpp"
#include "assets/AssetLoaderRegistry.hpp"
#include "assets/AssetPathUtilities.hpp"

#include <set>
#include <system_error>
#include <unordered_map>

namespace kb::assets {

std::size_t AssetDiscoveryService::DiscoverMountedAssets(
    const AssetMountTable& mounts,
    AssetRegistry& registry,
    const std::vector<std::unique_ptr<IAssetLoader>>& loaders,
    std::unordered_map<std::uint64_t, AssetManager::CachedAsset>& cache) {
    std::size_t discovered = 0;
    std::set<std::uint64_t> discoveredIds;
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

            const AssetId id = MakeAssetId(NormalizeAssetPath(*virtualPath) + ":" + std::string{ loader->Type() });
            AssetMetadata metadata{
                .id = id,
                .type = std::string{ loader->Type() },
                .name = entry.path().stem().string(),
                .virtualPath = *virtualPath,
                .physicalPath = entry.path(),
                .contentHash = AssetFileSystem::HashFile(entry.path()),
                .dependencies = {},
                .runtimeLoadable = true,
            };
            const auto previous = previousById.find(id.value);
            if (previous != previousById.end() &&
                (previous->second.contentHash != metadata.contentHash ||
                 previous->second.type != metadata.type ||
                 NormalizeAssetPath(previous->second.virtualPath) != NormalizeAssetPath(metadata.virtualPath))) {
                static_cast<void>(cache.erase(id.value));
            }
            if (registry.Upsert(std::move(metadata))) {
                ++discovered;
                discoveredIds.insert(id.value);
            }
        }
    }

    for (const AssetMetadata& metadata : previousAssets) {
        if (!AssetPathUtilities::IsMountedVirtualPath(mounts, metadata.virtualPath) || discoveredIds.contains(metadata.id.value)) {
            continue;
        }

        const std::filesystem::path physical = AssetPathUtilities::ResolvePhysicalPath(mounts, metadata);
        std::error_code error;
        if (physical.empty() || !std::filesystem::is_regular_file(physical, error) || AssetLoaderRegistry::FindByExtension(loaders, physical.extension()) == nullptr) {
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
