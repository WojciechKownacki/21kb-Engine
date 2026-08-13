#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace kb::audio {

inline constexpr std::array<std::string_view, 3U> kSupportedAudioClipExtensions{
    ".wav",
    ".flac",
    ".mp3",
};

[[nodiscard]] inline bool IsSupportedAudioClipExtension(std::string_view extension) noexcept {
    for (const std::string_view supported : kSupportedAudioClipExtensions) {
        if (extension.size() != supported.size()) {
            continue;
        }
        bool equal = true;
        for (std::size_t index = 0U; index < extension.size(); ++index) {
            char character = extension[index];
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
            if (character != supported[index]) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline std::string_view CanonicalAudioClipExtension(std::string_view extension) noexcept {
    for (const std::string_view supported : kSupportedAudioClipExtensions) {
        if (extension.size() != supported.size()) {
            continue;
        }
        bool equal = true;
        for (std::size_t index = 0U; index < extension.size(); ++index) {
            char character = extension[index];
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
            if (character != supported[index]) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return supported;
        }
    }
    return {};
}

} // namespace kb::audio
