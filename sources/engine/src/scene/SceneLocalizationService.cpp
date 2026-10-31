#include "scene/SceneLocalizationService.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/localization/LocalizationLocale.hpp"
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
    if (const auto locale = catalog.languages.find(language); locale != catalog.languages.end()) {
        const auto message = locale->second.find(key);
        return message == locale->second.end() ? nullptr : &message->second;
    }
    const auto wanted = kb::localization::LocalizationLocaleKey(language);
    if (!wanted) return nullptr;
    for (const auto& [tag, entries] : catalog.languages) {
        if (kb::localization::LocalizationLocaleKey(tag) != wanted) continue;
        const auto message = entries.find(key);
        return message == entries.end() ? nullptr : &message->second;
    }
    return nullptr;
}

struct FoundMessage {
    const kb::localization::LocalizationMessage* message = nullptr;
    // The language whose entry supplied the message; its plural rules pick the category.
    std::string_view language;
};

[[nodiscard]] FoundMessage FindWithFallback(const SceneState& state, std::string_view key) {
    if (!state.localizationCatalog.IsLoaded() || key.empty()) return {};
    const kb::localization::LocalizationCatalog& catalog = *state.localizationCatalog;
    std::string_view language = state.localizationLanguage;
    while (!language.empty()) {
        if (const auto* selected = FindMessage(catalog, language, key); selected != nullptr)
            return {selected, language};
        const auto separator = language.find_last_of("-_");
        if (separator == std::string_view::npos) break;
        language = language.substr(0U, separator);
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
    if (!state.localizationCatalog.IsLoaded() || !kb::localization::LocalizationLocaleKey(language)) return false;
    std::string_view candidate = language;
    while (!candidate.empty()) {
        const auto wanted = kb::localization::LocalizationLocaleKey(candidate);
        for (const auto& [tag, entries] : state.localizationCatalog->languages) {
            static_cast<void>(entries);
            if (kb::localization::LocalizationLocaleKey(tag) == wanted) {
                state.localizationLanguage = language;
                return true;
            }
        }
        const auto boundary = candidate.find_last_of("-_");
        if (boundary == std::string_view::npos) break;
        candidate = candidate.substr(0U, boundary);
    }
    return false;
}

std::string SceneLocalizationService::Language(const Scene& scene) {
    return SceneAccess::State(scene).localizationLanguage;
}

std::string SceneLocalizationService::FallbackLanguage(const Scene& scene) {
    const SceneState& state = SceneAccess::State(scene);
    return state.localizationCatalog.IsLoaded() ? state.localizationCatalog->fallbackLanguage : std::string{};
}

std::string SceneLocalizationService::MessageLanguage(const Scene& scene, std::string_view key, std::string_view number) {
    const auto& state = SceneAccess::State(scene);
    const auto found = FindWithFallback(state, key);
    if (!found.message) return state.localizationLanguage;
    if (number.empty()) return found.message->text.empty() ? state.localizationLanguage : std::string{found.language};
    const auto operands = kb::localization::ParsePluralOperands(number);
    if (!operands) return state.localizationLanguage;
    const auto category = kb::localization::PluralCategoryName(kb::localization::SelectPluralCategory(found.language, *operands));
    if (!found.message->plurals.contains(category) && !found.message->plurals.contains("other")) return state.localizationLanguage;
    return std::string{found.language};
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

std::uint64_t SceneLocalizationService::ResolveResource(const Scene& scene, std::uint64_t sourceAssetId) {
    const SceneState& state = SceneAccess::State(scene);
    if (sourceAssetId == 0U || !state.localizationCatalog.IsLoaded()) return sourceAssetId;
    const auto& catalog = *state.localizationCatalog;
    const auto lookup = [&](std::string_view language) -> std::optional<std::uint64_t> {
        const auto wanted = kb::localization::LocalizationLocaleKey(language);
        for (const auto& [tag, resources] : catalog.resources) {
            if (kb::localization::LocalizationLocaleKey(tag) != wanted) continue;
            const auto found = resources.find(sourceAssetId);
            if (found != resources.end()) return found->second;
        }
        return std::nullopt;
    };
    std::string_view language = state.localizationLanguage;
    while (!language.empty()) {
        if (const auto target = lookup(language)) return *target;
        const auto boundary = language.find_last_of("-_");
        if (boundary == std::string_view::npos) break;
        language = language.substr(0U, boundary);
    }
    return lookup(catalog.fallbackLanguage).value_or(sourceAssetId);
}

} // namespace kb::scene
