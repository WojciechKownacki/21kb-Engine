#pragma once

#include "engine/platform/PlatformCapabilities.hpp"
#include "engine/platform/PlatformLocale.hpp"

#include <cmath>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>

namespace kb::platform {

using ClipboardWriter = bool (*)(void* context, std::string_view text) noexcept;
using UrlOpener = bool (*)(void* context, std::string_view url) noexcept;
using VibrationWriter = bool (*)(void* context, float lowFrequency,
    float highFrequency) noexcept;

struct PlatformServiceCallbacks {
    ClipboardWriter clipboard = nullptr;
    UrlOpener openUrl = nullptr;
    VibrationWriter vibration = nullptr;
    void* context = nullptr;
};

class PlatformServices final {
public:
    PlatformServices(PlatformCapabilities capabilities, PlatformLocale locale,
        std::filesystem::path userDataPath,
        PlatformServiceCallbacks callbacks = {})
        : capabilities_(capabilities), locale_(std::move(locale)),
          userDataPath_(std::move(userDataPath)), callbacks_(callbacks) {}

    [[nodiscard]] constexpr PlatformCapabilities Capabilities() const noexcept {
        return capabilities_;
    }
    [[nodiscard]] std::optional<PlatformLocale> Locale() const {
        return capabilities_.Has(PlatformCapability::Locale) && locale_.IsValid()
            ? std::optional<PlatformLocale>{ locale_ } : std::nullopt;
    }
    [[nodiscard]] std::optional<std::filesystem::path> UserDataPath() const {
        return capabilities_.Has(PlatformCapability::UserDataPath) &&
                !userDataPath_.empty()
            ? std::optional<std::filesystem::path>{ userDataPath_ }
            : std::nullopt;
    }
    [[nodiscard]] bool SetClipboardText(std::string_view text) const noexcept {
        return capabilities_.Has(PlatformCapability::Clipboard) &&
            callbacks_.clipboard != nullptr && callbacks_.clipboard(
                callbacks_.context, text);
    }
    [[nodiscard]] bool OpenUrl(std::string_view url) const noexcept {
        return capabilities_.Has(PlatformCapability::OpenUrl) &&
            callbacks_.openUrl != nullptr && IsSafeUrl(url) && callbacks_.openUrl(
                callbacks_.context, url);
    }
    [[nodiscard]] bool SetVibration(float lowFrequency,
        float highFrequency) const noexcept {
        return capabilities_.Has(PlatformCapability::Vibration) &&
            callbacks_.vibration != nullptr && std::isfinite(lowFrequency) &&
            std::isfinite(highFrequency) && lowFrequency >= 0.0F &&
            lowFrequency <= 1.0F && highFrequency >= 0.0F &&
            highFrequency <= 1.0F && callbacks_.vibration(
                callbacks_.context, lowFrequency, highFrequency);
    }

private:
    [[nodiscard]] static bool IsSafeUrl(std::string_view url) noexcept {
        const bool hasScheme = url.starts_with("https://") ||
            url.starts_with("http://");
        if (!hasScheme || url.size() > 2'048U) return false;
        for (const unsigned char value : url) {
            if (value < 0x20U || value == 0x7FU) return false;
        }
        return true;
    }

    PlatformCapabilities capabilities_{};
    PlatformLocale locale_{};
    std::filesystem::path userDataPath_{};
    PlatformServiceCallbacks callbacks_{};
};

} // namespace kb::platform
