#include "engine/scene/UIAssetLoaders.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/UIAssetIO.hpp"

#include <memory>

namespace kb::scene {

std::string_view UIDocumentAssetLoader::Type() const noexcept { return kUIDocumentAssetType; }
std::type_index UIDocumentAssetLoader::PayloadType() const noexcept { return typeid(UIDocument); }
std::vector<std::string> UIDocumentAssetLoader::Extensions() const { return { kUIDocumentAssetExtension }; }
kb::assets::AssetLoadResult UIDocumentAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    auto document = UIAssetIO::LoadDocument(request.resolvedPath);
    return document ? kb::assets::AssetLoadResult{ std::make_shared<UIDocument>(std::move(*document)), {} }
                    : kb::assets::AssetLoadResult{ {}, "UI document could not be loaded or parsed." };
}
std::vector<kb::assets::AssetId> UIDocumentAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    const auto document = UIAssetIO::LoadDocument(metadata.physicalPath);
    if (!document || document->styleAssetId == 0U) return {};
    const kb::assets::AssetId style{ document->styleAssetId };
    const kb::assets::AssetMetadata* styleMetadata = registry.Find(style);
    return styleMetadata != nullptr && styleMetadata->type == kUIStyleAssetType
        ? std::vector<kb::assets::AssetId>{ style }
        : std::vector<kb::assets::AssetId>{};
}

std::string_view UIStyleAssetLoader::Type() const noexcept { return kUIStyleAssetType; }
std::type_index UIStyleAssetLoader::PayloadType() const noexcept { return typeid(UIStyleAsset); }
std::vector<std::string> UIStyleAssetLoader::Extensions() const { return { kUIStyleAssetExtension }; }
kb::assets::AssetLoadResult UIStyleAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    auto style = UIAssetIO::LoadStyle(request.resolvedPath);
    return style ? kb::assets::AssetLoadResult{ std::make_shared<UIStyleAsset>(std::move(*style)), {} }
                 : kb::assets::AssetLoadResult{ {}, "UI style asset could not be loaded or parsed." };
}

} // namespace kb::scene
