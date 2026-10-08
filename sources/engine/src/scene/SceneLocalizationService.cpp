#include "scene/SceneLocalizationService.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/localization/PluralRules.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <optional>
#include <string>

namespace kb::scene {
namespace {

[[nodiscard]] const kb::localization::LocalizationMessage* FindMessage(
    const kb::localization::LocalizationCatalog& catalog, std::string_view language, std::string_view key) {
    const auto locale = catalog.languages.find(language);
    if (locale == catalog.languages.end()) return nullptr;
    const auto message = locale->second.find(key);
    return message == locale->second.end() ? nullptr : &message->second;
}

struct FoundMessage {
    const kb::localization::LocalizationMessage* message = nullptr;
    // The language whose entry supplied the message; its plural rules pick the category.
    std::string_view language;
};

[[nodiscard]] FoundMessage FindWithFallback(const SceneState& state, std::string_view key) {
    if (!state.localizationCatalog.IsLoaded() || key.empty()) return {};
    const kb::localization::LocalizationCatalog& catalog = *state.localizationCatalog;
    if (const auto* selected = FindMessage(catalog, state.localizationLanguage, key); selected != nullptr) {
        return { selected, state.localizationLanguage };
    }
    return { FindMessage(catalog, catalog.fallbackLanguage, key), catalog.fallbackLanguage };
}

[[nodiscard]] std::string ReplaceCount(std::string value, std::string_view countText) {
    constexpr std::string_view marker = "{count}";
    std::size_t position = 0U;
    while ((position = value.find(marker, position)) != std::string::npos) {
        value.replace(position, marker.size(), countText);
        position += countText.size();
    }
    return value;
}

[[nodiscard]] std::string FormatPluralMessage(const SceneState& state, std::string_view key,
    const kb::localization::PluralOperands& operands, std::string_view countText) {
    const FoundMessage found = FindWithFallback(state, key);
    if (found.message == nullptr || found.message->plurals.empty()) return std::string{ key };
    const kb::localization::PluralCategory category = kb::localization::SelectPluralCategory(found.language, operands);
    auto selected = found.message->plurals.find(kb::localization::PluralCategoryName(category));
    if (selected == found.message->plurals.end()) selected = found.message->plurals.find("other");
    if (selected == found.message->plurals.end()) return std::string{ key };
    return ReplaceCount(selected->second, countText);
}

} // namespace

bool SceneLocalizationService::SetCatalog(Scene& scene, std::uint64_t assetId) {
    if (assetId == 0U) return false;
    const kb::assets::AssetId id{ assetId };
    const auto* metadata = scene.Assets().Manager().Registry().Find(id);
    if (metadata == nullptr || metadata->type != kb::localization::kLocalizationCatalogAssetType) return false;
    auto catalog = scene.Assets().Manager().Load<kb::localization::LocalizationCatalog>(id);
    if (!catalog.IsLoaded()) return false;
    SceneState& state = SceneAccess::State(scene);
    state.localizationCatalog = std::move(catalog);
    state.localizationCatalogGeneration = scene.Assets().Manager().LoadGeneration(id);
    state.localizationLanguage = state.localizationCatalog->fallbackLanguage;
    return true;
}

std::uint64_t SceneLocalizationService::Catalog(const Scene& scene) noexcept {
    return SceneAccess::State(scene).localizationCatalog.Id().value;
}

bool SceneLocalizationService::SetLanguage(Scene& scene, std::string_view language) {
    SceneState& state = SceneAccess::State(scene);
    if (!state.localizationCatalog.IsLoaded() || !state.localizationCatalog->languages.contains(std::string{ language })) return false;
    state.localizationLanguage = language;
    return true;
}

std::string SceneLocalizationService::Language(const Scene& scene) {
    return SceneAccess::State(scene).localizationLanguage;
}

std::string SceneLocalizationService::FallbackLanguage(const Scene& scene) {
    const SceneState& state = SceneAccess::State(scene);
    return state.localizationCatalog.IsLoaded() ? state.localizationCatalog->fallbackLanguage : std::string{};
}

std::string SceneLocalizationService::Translate(const Scene& scene, std::string_view key) {
    const SceneState& state = SceneAccess::State(scene);
    const auto* message = FindWithFallback(state, key).message;
    if (message == nullptr || message->text.empty()) return std::string{ key };
    return message->text;
}

std::string SceneLocalizationService::FormatPlural(const Scene& scene, std::string_view key, std::int64_t count) {
    return FormatPluralMessage(SceneAccess::State(scene), key, kb::localization::PluralOperandsFromInteger(count), std::to_string(count));
}

std::string SceneLocalizationService::FormatPluralNumber(const Scene& scene, std::string_view key, std::string_view number) {
    const std::optional<kb::localization::PluralOperands> operands = kb::localization::ParsePluralOperands(number);
    if (!operands.has_value()) return std::string{ key };
    return FormatPluralMessage(SceneAccess::State(scene), key, *operands, number);
}

} // namespace kb::scene
