#pragma once

#include "engine/platform/PlatformCapabilities.hpp"

#include <string_view>

namespace kb::platform { enum class OptionalPlatformService : std::uint8_t { Achievements, CloudSave, Dlc, User }; class IPlatformAdapter { public: virtual ~IPlatformAdapter()=default; [[nodiscard]] virtual bool IsAvailable(OptionalPlatformService service) const noexcept=0; [[nodiscard]] virtual bool UnlockAchievement(std::string_view id)=0; }; }
