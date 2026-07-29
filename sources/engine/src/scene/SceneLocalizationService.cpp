#include "scene/SceneLocalizationService.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/localization/LocalizationCatalog.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
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

[[nodiscard]] const kb::localization::LocalizationMessage* FindWithFallback(
    const SceneState& state, std::string_view key) {
    if (!state.localizationCatalog.IsLoaded() || key.empty()) return nullptr;
    const kb::localization::LocalizationCatalog& catalog = *state.localizationCatalog;
    if (const auto* selected = FindMessage(catalog, state.localizationLanguage, key); selected != nullptr) return selected;
    return FindMessage(catalog, catalog.fallbackLanguage, key);
}

[[nodiscard]] std::string PluralCategory(std::string_view language, std::int64_t count) {
    const std::uint64_t absolute = count < 0
        ? static_cast<std::uint64_t>(-(count + 1)) + 1U
        : static_cast<std::uint64_t>(count);
    if (language.rfind("pl", 0U) == 0U) {
        if (absolute == 1) return "one";
        const std::uint64_t mod10 = absolute % 10U;
        const std::uint64_t mod100 = absolute % 100U;
        if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return "few";
        return "many";
    }
    return absolute == 1 ? "one" : "other";
}

[[nodiscard]] std::string ReplaceCount(std::string value, std::int64_t count) {
    const std::string countText = std::to_string(count);
    constexpr std::string_view marker = "{count}";
    std::size_t position = 0U;
    while ((position = value.find(marker, position)) != std::string::npos) {
        value.replace(position, marker.size(), countText);
        position += countText.size();
    }
    return value;
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
    const auto* message = FindWithFallback(state, key);
    if (message == nullptr || message->text.empty()) return std::string{ key };
    return message->text;
}

std::string SceneLocalizationService::FormatPlural(const Scene& scene, std::string_view key, std::int64_t count) {
    const SceneState& state = SceneAccess::State(scene);
    const auto* message = FindWithFallback(state, key);
    if (message == nullptr || message->plurals.empty()) return std::string{ key };
    const std::string language = state.localizationCatalog->languages.contains(state.localizationLanguage)
        ? state.localizationLanguage : state.localizationCatalog->fallbackLanguage;
    const auto category = message->plurals.find(PluralCategory(language, count));
    const auto other = message->plurals.find("other");
    return ReplaceCount(category != message->plurals.end() ? category->second : other->second, count);
}

} // namespace kb::scene
