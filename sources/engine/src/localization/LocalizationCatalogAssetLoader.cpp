#include "engine/localization/LocalizationCatalogAssetLoader.hpp"

#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/localization/LocalizationCatalogIO.hpp"

#include "engine/assets/AssetRegistry.hpp"

#include <memory>
#include <set>

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

namespace {
std::optional<std::string> ValidateResources(const std::optional<LocalizationCatalog>& catalog,
    const kb::assets::AssetRegistry& registry) {
    if (!catalog) return "Localization catalog could not be parsed.";
    for (const auto& [language, resources] : catalog->resources) {
        static_cast<void>(language);
        for (const auto& [sourceId, targetId] : resources) {
            const auto* source = registry.Find(kb::assets::AssetId{sourceId});
            const auto* target = registry.Find(kb::assets::AssetId{targetId});
            if (!source || !target) return "Localized resource was not discovered.";
            if (!source->runtimeLoadable || !target->runtimeLoadable || source->type != target->type ||
                source->type == kLocalizationCatalogAssetType ||
                (source->type == "ImportedAsset" && (source->importCategory.empty() || source->importCategory != target->importCategory)))
                return "Localized resources must have matching runtime asset types and cannot be catalogs.";
        }
    }
    return std::nullopt;
}
}

std::vector<kb::assets::AssetId> LocalizationCatalogAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    static_cast<void>(registry);
    const auto catalog = LocalizationCatalogIO::Load(metadata.physicalPath);
    std::set<std::uint64_t> ids;
    if (catalog) for (const auto& [language, resources] : catalog->resources) {
        static_cast<void>(language);
        for (const auto& [source, target] : resources) { ids.insert(source); ids.insert(target); }
    }
    std::vector<kb::assets::AssetId> result;
    for (const auto id : ids) result.push_back(kb::assets::AssetId{id});
    return result;
}

std::optional<std::string> LocalizationCatalogAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    return ValidateResources(LocalizationCatalogIO::Load(metadata.physicalPath), registry);
}

std::optional<std::string> LocalizationCatalogAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request, const kb::assets::AssetRegistry& registry) const {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes, error)) return error;
    return ValidateResources(LocalizationCatalogIO::Load(bytes), registry);
}

} // namespace kb::localization
