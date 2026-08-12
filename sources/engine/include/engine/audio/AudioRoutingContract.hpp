#pragma once

#include <cstddef>
#include <string_view>

namespace kb::audio {

inline constexpr std::size_t kMaxAudioMixerNameBytes = 255U;

[[nodiscard]] inline bool IsAudioMixerNameTokenValid(std::string_view name) noexcept {
    if (name.empty() || name == "-" || name.size() > kMaxAudioMixerNameBytes) {
        return false;
    }
    for (const char character : name) {
        const unsigned char code = static_cast<unsigned char>(character);
        if (code <= 0x20U || character == '#') {
            return false;
        }
    }
    return true;
}

} // namespace kb::audio
