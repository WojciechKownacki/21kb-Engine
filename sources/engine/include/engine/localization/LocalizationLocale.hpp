#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kb::localization {

// Catalog matching ignores ASCII case and accepts either common subtag separator.
// Reject empty subtags; retain the authored/requested spelling in public state.
[[nodiscard]] inline std::optional<std::string> LocalizationLocaleKey(std::string_view language) {
    if (language.empty() || language.size() > 32U) return std::nullopt;
    std::string result;
    result.reserve(language.size());
    bool separator = true;
    for (char value : language) {
        if (value == '-' || value == '_') {
            if (separator) return std::nullopt;
            result.push_back('-');
            separator = true;
        } else {
            if (value >= 'A' && value <= 'Z') value = static_cast<char>(value + ('a' - 'A'));
            if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9'))) return std::nullopt;
            result.push_back(value);
            separator = false;
        }
    }
    return separator ? std::nullopt : std::optional<std::string>{std::move(result)};
}

} // namespace kb::localization
