#pragma once

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMountTable.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace kb::assets {

class AssetDiscoveryService {
public:
    AssetDiscoveryService() = delete;

    [[nodiscard]] static std::size_t DiscoverMountedAssets(
        const AssetMountTable& mounts,
        AssetRegistry& registry,
        const std::vector<std::unique_ptr<IAssetLoader>>& loaders,
        std::unordered_map<std::uint64_t, AssetManager::CachedAsset>& cache);

    [[nodiscard]] static std::vector<std::filesystem::path> VirtualFolders(
        const AssetMountTable& mounts,
        const AssetRegistry& registry);
};

} // namespace kb::assets
