#include "profiler/ProfilerNumber.hpp"

#include "localization/EditorLocalization.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace kb::editor {
namespace {

// The languages of the Language list that write 0,5 and not 0.5 (CLDR).
constexpr std::array<std::string_view, 25> kCommaLanguages{
    "bg", "cs", "da", "de", "el", "es", "fi", "fr", "hr", "hu", "id", "it", "nb",
    "nl", "pl", "pt-BR", "pt-PT", "ro", "ru", "sk", "sr", "sv", "tr", "uk", "vi" };

} // namespace

bool ProfilerNumber::UsesDecimalComma(std::string_view language) noexcept {
    return std::ranges::find(kCommaLanguages, language) != kCommaLanguages.end();
}

std::string ProfilerNumber::Localized(std::string text, std::string_view language) {
    if (UsesDecimalComma(language)) std::ranges::replace(text, '.', ',');
    return text;
}

std::string ProfilerNumber::Fixed(double value, int decimals) {
    return Localized(std::format("{:.{}f}", value, decimals), EditorLocalization::Current());
}

std::string ProfilerNumber::General(double value) {
    return Localized(std::format("{:g}", value), EditorLocalization::Current());
}

} // namespace kb::editor
