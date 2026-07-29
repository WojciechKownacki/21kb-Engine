#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace kb::localization {

inline constexpr const char* kLocalizationCatalogAssetExtension = ".kbloc";
inline constexpr const char* kLocalizationCatalogAssetType = "LocalizationCatalog";

// Plural categories use CLDR-compatible names. A catalog may omit categories
// that cannot occur in its language, but must always author `other`.
struct LocalizationMessage {
    std::string text;
    std::map<std::string, std::string, std::less<>> plurals;
};

struct LocalizationCatalog {
    static constexpr std::uint32_t kSchemaVersion = 1U;
    std::uint32_t schemaVersion = kSchemaVersion;
    std::string fallbackLanguage;
    std::map<std::string, std::map<std::string, LocalizationMessage, std::less<>>, std::less<>> languages;
};

} // namespace kb::localization
