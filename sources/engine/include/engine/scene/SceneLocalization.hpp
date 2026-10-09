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
// Plural messages pick their category with the CLDR plural rules of the
// language whose entry supplied the message.
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
    // Plural formatting for a decimal written as text ("1.5", "2.00", "1.2c6"): visible fraction digits
    // select the category as CLDR defines, and the text replaces `{count}` unchanged. Malformed numbers
    // return the key.
    [[nodiscard]] std::string FormatPluralNumber(std::string_view key, std::string_view number) const;
private:
    Scene& scene_;
};

} // namespace kb::scene
