#pragma once

#include <cstdint>
#include <string>

namespace kb::platform { struct PlatformLocale { std::string language; std::string region; std::int16_t utcOffsetMinutes = 0; [[nodiscard]] bool IsValid() const noexcept { return language.size()>=2U&&language.size()<=8U&&region.size()==2U&&utcOffsetMinutes>=-720&&utcOffsetMinutes<=840; } }; struct SafeDateTime { std::int64_t unixSeconds = 0; [[nodiscard]] constexpr std::int64_t ToLocalSeconds(PlatformLocale locale) const noexcept{return unixSeconds+static_cast<std::int64_t>(locale.utcOffsetMinutes)*60;} }; }
