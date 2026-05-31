#include "assets/AssetLoaderRegistry.hpp"

#include "assets/AssetPathUtilities.hpp"

#include <algorithm>

namespace kb::assets {

bool AssetLoaderRegistry::Register(std::vector<std::unique_ptr<IAssetLoader>>& loaders, std::unique_ptr<IAssetLoader> loader) {
    if (loader == nullptr || loader->Type().empty()) {
        return false;
    }

    const std::string_view type = loader->Type();
    const auto existing = std::ranges::find_if(loaders, [type](const std::unique_ptr<IAssetLoader>& candidate) {
        return candidate->Type() == type;
    });
    if (existing != loaders.end()) {
        *existing = std::move(loader);
        return true;
    }

    loaders.push_back(std::move(loader));
    return true;
}

IAssetLoader* AssetLoaderRegistry::FindByType(const std::vector<std::unique_ptr<IAssetLoader>>& loaders, std::string_view type) noexcept {
    const auto iterator = std::ranges::find_if(loaders, [type](const std::unique_ptr<IAssetLoader>& loader) {
        return loader->Type() == type;
    });
    return iterator == loaders.end() ? nullptr : iterator->get();
}

IAssetLoader* AssetLoaderRegistry::FindByExtension(
    const std::vector<std::unique_ptr<IAssetLoader>>& loaders,
    const std::filesystem::path& extension) noexcept {
    const std::string normalized = AssetPathUtilities::LowerExtension(extension);
    const auto iterator = std::ranges::find_if(loaders, [&normalized](const std::unique_ptr<IAssetLoader>& loader) {
        const std::vector<std::string> extensions = loader->Extensions();
        return std::ranges::any_of(extensions, [&normalized](const std::string& candidate) {
            return AssetPathUtilities::LowerExtension(candidate) == normalized;
        });
    });
    return iterator == loaders.end() ? nullptr : iterator->get();
}

} // namespace kb::assets
