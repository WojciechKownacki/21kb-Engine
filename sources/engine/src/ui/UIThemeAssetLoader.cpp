#include "engine/ui/UIThemeAssetLoader.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/ui/UIThemeIO.hpp"

#include <memory>
#include <set>

namespace kb::scene {
namespace {

template <typename Visitor>
bool VisitAssets(const UITheme& theme, Visitor&& visit) {
    auto properties = [&](std::span<const UIThemeProperty> list) {
        for (const auto& item : list) {
            const auto* descriptor = FindUIComponentProperty(item.key.component, item.key.property);
            if (!descriptor || descriptor->type != UIComponentPropertyType::Asset) continue;
            const auto* id = std::get_if<std::uint64_t>(&item.value);
            if (!id || !descriptor->assetKind || !visit(*id, *descriptor->assetKind)) return false;
        }
        return true;
    };
    if (!properties(theme.properties)) return false;
    for (const auto& variant : theme.variants) if (!properties(variant.properties)) return false;
    return true;
}

std::optional<std::string> Validate(const std::optional<UITheme>& theme, const kb::assets::AssetRegistry& registry) {
    if (!theme) return "UI theme could not be parsed.";
    const bool valid = VisitAssets(*theme, [&](std::uint64_t id, kb::assets::AssetKind kind) {
        if (!id) return true;
        const auto* metadata = registry.Find(kb::assets::AssetId{id});
        return metadata && metadata->runtimeLoadable && kb::assets::AssetMatchesKind(*metadata, kind);
    });
    return valid ? std::nullopt : std::optional<std::string>{"UI theme references a missing or incompatible asset."};
}

} // namespace

std::string_view UIThemeAssetLoader::Type() const noexcept { return kUIThemeAssetType; }
std::type_index UIThemeAssetLoader::PayloadType() const noexcept { return typeid(UITheme); }
std::vector<std::string> UIThemeAssetLoader::Extensions() const { return {std::string{kUIThemeAssetExtension}}; }
kb::assets::AssetLoadResult UIThemeAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes, error)) return {{}, std::move(error)};
    auto theme = UIThemeIO::Load(bytes);
    return theme ? kb::assets::AssetLoadResult{std::make_shared<UITheme>(std::move(*theme)), {}}
                 : kb::assets::AssetLoadResult{{}, "UI theme could not be parsed."};
}
std::vector<kb::assets::AssetId> UIThemeAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry&) const {
    const auto theme = UIThemeIO::Load(metadata.physicalPath);
    std::set<std::uint64_t> ids;
    if (theme) static_cast<void>(VisitAssets(*theme, [&](std::uint64_t id, kb::assets::AssetKind) {
        if (id) ids.insert(id);
        return true;
    }));
    std::vector<kb::assets::AssetId> result;
    for (auto id : ids) result.push_back(kb::assets::AssetId{id});
    return result;
}
std::optional<std::string> UIThemeAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata, const kb::assets::AssetRegistry& registry) const {
    return Validate(UIThemeIO::Load(metadata.physicalPath), registry);
}
std::optional<std::string> UIThemeAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request, const kb::assets::AssetRegistry& registry) const {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!request.ReadSourceBytes(bytes, error)) return error;
    return Validate(UIThemeIO::Load(bytes), registry);
}

} // namespace kb::scene
