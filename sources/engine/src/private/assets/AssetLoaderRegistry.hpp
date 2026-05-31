#pragma once

#include "engine/assets/IAssetLoader.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace kb::assets {

class AssetLoaderRegistry {
public:
    AssetLoaderRegistry() = delete;

    [[nodiscard]] static bool Register(std::vector<std::unique_ptr<IAssetLoader>>& loaders, std::unique_ptr<IAssetLoader> loader);
    [[nodiscard]] static IAssetLoader* FindByType(const std::vector<std::unique_ptr<IAssetLoader>>& loaders, std::string_view type) noexcept;
    [[nodiscard]] static IAssetLoader* FindByExtension(
        const std::vector<std::unique_ptr<IAssetLoader>>& loaders,
        const std::filesystem::path& extension) noexcept;
};

} // namespace kb::assets
