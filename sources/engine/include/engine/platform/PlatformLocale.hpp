#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace kb::platform {

struct PlatformLocale {
    std::string language;
    std::string region;
    std::int16_t utcOffsetMinutes = 0;

    [[nodiscard]] bool IsValid() const noexcept {
        const auto isAsciiAlphabetic = [](const std::string& value) noexcept {
            for (const unsigned char character : value) {
                if ((character < 'A' || character > 'Z') &&
                    (character < 'a' || character > 'z')) {
                    return false;
                }
            }
            return true;
        };
        return language.size() >= 2U && language.size() <= 8U &&
            region.size() == 2U && isAsciiAlphabetic(language) &&
            isAsciiAlphabetic(region) && utcOffsetMinutes >= -720 &&
            utcOffsetMinutes <= 840;
    }
};

struct SafeDateTime {
    std::int64_t unixSeconds = 0;

    [[nodiscard]] std::optional<std::int64_t> ToLocalSeconds(
        const PlatformLocale& locale) const noexcept {
        if (!locale.IsValid()) return std::nullopt;
        const std::int64_t offsetSeconds =
            static_cast<std::int64_t>(locale.utcOffsetMinutes) * 60;
        if ((offsetSeconds > 0 && unixSeconds > std::numeric_limits<std::int64_t>::max() - offsetSeconds) ||
            (offsetSeconds < 0 && unixSeconds < std::numeric_limits<std::int64_t>::min() - offsetSeconds)) {
            return std::nullopt;
        }
        return unixSeconds + offsetSeconds;
    }
};

} // namespace kb::platform
