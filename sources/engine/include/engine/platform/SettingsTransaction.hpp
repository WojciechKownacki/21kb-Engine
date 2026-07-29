#pragma once

#include "engine/platform/PlatformCapabilities.hpp"

namespace kb::platform {
struct RuntimeSettings { float masterVolume = 1.0F; bool vibration = false; };
class SettingsTransaction final { public: explicit SettingsTransaction(RuntimeSettings current) noexcept:committed_(current),pending_(current){} [[nodiscard]] RuntimeSettings& Pending() noexcept{return pending_;} [[nodiscard]] const RuntimeSettings& Current() const noexcept{return committed_;} [[nodiscard]] bool Apply(PlatformCapabilities capabilities) noexcept {if(pending_.masterVolume<0.0F||pending_.masterVolume>1.0F||(pending_.vibration&&!capabilities.Has(PlatformCapability::Vibration)))return false;committed_=pending_;return true;} void Revert() noexcept{pending_=committed_;} private:RuntimeSettings committed_;RuntimeSettings pending_;};
} // namespace kb::platform
