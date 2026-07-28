#pragma once

#include "engine/assets/AssetManager.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace kb::assets {

class AssetRuntimeLoadService {
public:
    AssetRuntimeLoadService() = delete;

    [[nodiscard]] static std::shared_ptr<void> LoadUntyped(
        AssetId id,
        std::type_index expectedType,
        const AssetRegistry& registry,
        const AssetMountTable& mounts,
        const std::vector<std::unique_ptr<IAssetLoader>>& loaders,
        std::unordered_map<std::uint64_t, AssetManager::CachedAsset>& cache,
        std::mutex& loaderExecutionMutex,
        std::string& error);
};

} // namespace kb::assets
