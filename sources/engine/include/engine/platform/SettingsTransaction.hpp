#pragma once

#include "engine/platform/PlatformCapabilities.hpp"

#include <cmath>
#include <cstdint>

namespace kb::platform {
struct RuntimeSettings { float masterVolume = 1.0F; bool vibration = false; std::uint16_t videoWidth = 1280U; std::uint16_t videoHeight = 720U; bool fullscreen = false; bool verticalSync = true; float mouseSensitivity = 1.0F; bool invertLookY = false; };
struct DeviceSettingsCapabilities { std::uint16_t maximumVideoWidth = 7680U; std::uint16_t maximumVideoHeight = 4320U; bool fullscreen = true; };
class SettingsTransaction final { public: explicit SettingsTransaction(RuntimeSettings current) noexcept:committed_(current),pending_(current){} [[nodiscard]] RuntimeSettings& Pending() noexcept{return pending_;} [[nodiscard]] const RuntimeSettings& Current() const noexcept{return committed_;} [[nodiscard]] bool Apply(PlatformCapabilities capabilities, DeviceSettingsCapabilities device = {}) noexcept {if(!IsValid(pending_,capabilities,device))return false;committed_=pending_;return true;} void Revert() noexcept{pending_=committed_;} private: [[nodiscard]] static bool IsValid(const RuntimeSettings& settings, PlatformCapabilities capabilities, DeviceSettingsCapabilities device) noexcept { return std::isfinite(settings.masterVolume)&&settings.masterVolume>=0.0F&&settings.masterVolume<=1.0F&&(!settings.vibration||capabilities.Has(PlatformCapability::Vibration))&&settings.videoWidth>=640U&&settings.videoHeight>=480U&&settings.videoWidth<=device.maximumVideoWidth&&settings.videoHeight<=device.maximumVideoHeight&&(!settings.fullscreen||device.fullscreen)&&std::isfinite(settings.mouseSensitivity)&&settings.mouseSensitivity>=0.1F&&settings.mouseSensitivity<=10.0F; } RuntimeSettings committed_;RuntimeSettings pending_;};
} // namespace kb::platform
