#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::scene {

class Scene;

// Scene-owned localization state. All display text is looked up by an authored
// key from one LocalizationCatalog asset; a missing selected-language entry
// falls back to the catalog's declared fallback language, then returns the key
// unchanged so missing content is visible instead of silently becoming blank.
class SceneLocalization final {
public:
    explicit SceneLocalization(Scene& scene) noexcept;
    [[nodiscard]] bool SetCatalog(std::uint64_t assetId);
    [[nodiscard]] std::uint64_t Catalog() const noexcept;
    [[nodiscard]] bool SetLanguage(std::string_view language);
    [[nodiscard]] std::string Language() const;
    [[nodiscard]] std::string FallbackLanguage() const;
    [[nodiscard]] std::string Translate(std::string_view key) const;
    [[nodiscard]] std::string FormatPlural(std::string_view key, std::int64_t count) const;
private:
    Scene& scene_;
};

} // namespace kb::scene
