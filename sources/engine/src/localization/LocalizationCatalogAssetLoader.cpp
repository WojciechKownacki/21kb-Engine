#include "engine/localization/LocalizationCatalogAssetLoader.hpp"

#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/localization/LocalizationCatalogIO.hpp"

#include <memory>

namespace kb::localization {

std::string_view LocalizationCatalogAssetLoader::Type() const noexcept { return kLocalizationCatalogAssetType; }
std::type_index LocalizationCatalogAssetLoader::PayloadType() const noexcept { return typeid(LocalizationCatalog); }
std::vector<std::string> LocalizationCatalogAssetLoader::Extensions() const { return { kLocalizationCatalogAssetExtension }; }
kb::assets::AssetLoadResult LocalizationCatalogAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ {}, std::move(error) };
    }
    auto catalog = LocalizationCatalogIO::Load(sourceBytes);
    return catalog ? kb::assets::AssetLoadResult{ std::make_shared<LocalizationCatalog>(std::move(*catalog)), {} }
                   : kb::assets::AssetLoadResult{ {}, "Localization catalog could not be loaded or parsed." };
}

} // namespace kb::localization
