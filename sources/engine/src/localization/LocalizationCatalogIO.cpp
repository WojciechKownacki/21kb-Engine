#include "engine/localization/LocalizationCatalogIO.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <span>
#include <unordered_set>
#include <vector>

namespace kb::localization {
namespace {

[[nodiscard]] bool ValidLanguage(std::string_view language) {
    if (language.empty() || language.size() > 32U) return false;
    for (const unsigned char value : language) {
        if (!std::isalnum(value) && value != '-' && value != '_') return false;
    }
    return true;
}

[[nodiscard]] bool ValidKey(std::string_view key) {
    if (key.empty() || key.size() > 256U) return false;
    for (const unsigned char value : key) {
        if (!std::isalnum(value) && value != '.' && value != '_' && value != '-') return false;
    }
    return true;
}

[[nodiscard]] bool ValidCategory(std::string_view category) {
    return category == "zero" || category == "one" || category == "two" || category == "few" ||
        category == "many" || category == "other";
}

[[nodiscard]] bool ValidCatalog(const LocalizationCatalog& catalog) {
    if (catalog.schemaVersion != LocalizationCatalog::kSchemaVersion || !ValidLanguage(catalog.fallbackLanguage)) return false;
    const auto fallback = catalog.languages.find(catalog.fallbackLanguage);
    if (fallback == catalog.languages.end()) return false;
    for (const auto& [language, entries] : catalog.languages) {
        if (!ValidLanguage(language) || entries.empty()) return false;
        for (const auto& [key, message] : entries) {
            if (!ValidKey(key) || (message.text.empty() == message.plurals.empty())) return false;
            if (!message.plurals.empty()) {
                if (!message.plurals.contains("other")) return false;
                for (const auto& [category, text] : message.plurals) {
                    if (!ValidCategory(category) || text.empty()) return false;
                }
            }
        }
    }
    return true;
}

} // namespace

std::optional<LocalizationCatalog> LocalizationCatalogIO::Load(const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes = kb::scene::SceneAssetBinaryIO::ReadAllBytes(path);
    if (bytes.empty()) return std::nullopt;
    return Load(bytes);
}

std::optional<LocalizationCatalog> LocalizationCatalogIO::Load(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return std::nullopt;
    std::istringstream input{ std::string{ reinterpret_cast<const char*>(bytes.data()), bytes.size() } };
    LocalizationCatalog catalog{};
    std::string token;
    std::string currentLanguage;
    if (!(input >> token >> catalog.schemaVersion) || token != "schema") return std::nullopt;
    while (input >> token) {
        if (token == "fallback") {
            if (!(input >> std::quoted(catalog.fallbackLanguage)) || !currentLanguage.empty()) return std::nullopt;
        } else if (token == "language") {
            if (!(input >> std::quoted(currentLanguage)) || !ValidLanguage(currentLanguage) || catalog.languages.contains(currentLanguage)) return std::nullopt;
            catalog.languages.emplace(currentLanguage, decltype(catalog.languages)::mapped_type{});
        } else if (token == "text") {
            std::string key;
            std::string value;
            if (currentLanguage.empty() || !(input >> std::quoted(key) >> std::quoted(value)) || !ValidKey(key) || value.empty()) return std::nullopt;
            LocalizationMessage& message = catalog.languages.at(currentLanguage)[key];
            if (!message.text.empty() || !message.plurals.empty()) return std::nullopt;
            message.text = std::move(value);
        } else if (token == "plural") {
            std::string key;
            std::string category;
            std::string value;
            if (currentLanguage.empty() || !(input >> std::quoted(key) >> std::quoted(category) >> std::quoted(value)) ||
                !ValidKey(key) || !ValidCategory(category) || value.empty()) return std::nullopt;
            LocalizationMessage& message = catalog.languages.at(currentLanguage)[key];
            if (!message.text.empty() || !message.plurals.emplace(std::move(category), std::move(value)).second) return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    return ValidCatalog(catalog) ? std::optional<LocalizationCatalog>{ std::move(catalog) } : std::nullopt;
}

bool LocalizationCatalogIO::Save(const std::filesystem::path& path, const LocalizationCatalog& catalog) {
    if (!ValidCatalog(catalog)) return false;
    std::ostringstream output;
    output << "schema " << catalog.schemaVersion << '\n';
    output << "fallback " << std::quoted(catalog.fallbackLanguage) << '\n';
    for (const auto& [language, entries] : catalog.languages) {
        output << "language " << std::quoted(language) << '\n';
        for (const auto& [key, message] : entries) {
            if (!message.text.empty()) {
                output << "text " << std::quoted(key) << ' ' << std::quoted(message.text) << '\n';
            } else {
                for (const auto& [category, value] : message.plurals) {
                    output << "plural " << std::quoted(key) << ' ' << std::quoted(category) << ' ' << std::quoted(value) << '\n';
                }
            }
        }
    }
    const std::string text = output.str();
    return kb::scene::SceneAssetBinaryIO::WriteBytesAtomically(path,
        std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

} // namespace kb::localization
