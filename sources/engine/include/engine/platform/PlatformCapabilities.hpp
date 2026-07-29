#pragma once

#include <cstdint>

namespace kb::platform {
enum class PlatformCapability : std::uint32_t { Locale=1U<<0U, UserDataPath=1U<<1U, Clipboard=1U<<2U, OpenUrl=1U<<3U, Vibration=1U<<4U };
struct PlatformCapabilities { std::uint32_t flags = 0U; [[nodiscard]] constexpr bool Has(PlatformCapability capability) const noexcept { return (flags&static_cast<std::uint32_t>(capability))!=0U; } };
} // namespace kb::platform
