#include "engine/scene/SceneLocalization.hpp"

#include "scene/SceneLocalizationService.hpp"

namespace kb::scene {

SceneLocalization::SceneLocalization(Scene& scene) noexcept : scene_(scene) {}
bool SceneLocalization::SetCatalog(std::uint64_t assetId) { return SceneLocalizationService::SetCatalog(scene_, assetId); }
std::uint64_t SceneLocalization::Catalog() const noexcept { return SceneLocalizationService::Catalog(scene_); }
bool SceneLocalization::SetLanguage(std::string_view language) { return SceneLocalizationService::SetLanguage(scene_, language); }
std::string SceneLocalization::Language() const { return SceneLocalizationService::Language(scene_); }
std::string SceneLocalization::FallbackLanguage() const { return SceneLocalizationService::FallbackLanguage(scene_); }
std::string SceneLocalization::Translate(std::string_view key) const { return SceneLocalizationService::Translate(scene_, key); }
std::string SceneLocalization::FormatPlural(std::string_view key, std::int64_t count) const { return SceneLocalizationService::FormatPlural(scene_, key, count); }

} // namespace kb::scene
