#pragma once

#include <miniaudio.h>

#include <string_view>

namespace kb::audio_miniaudio {

[[nodiscard]] inline ma_encoding_format EncodingFormatForAudioExtension(
    std::string_view extension) noexcept {
    if (extension == ".wav") {
        return ma_encoding_format_wav;
    }
    if (extension == ".flac") {
        return ma_encoding_format_flac;
    }
    if (extension == ".mp3") {
        return ma_encoding_format_mp3;
    }
    return ma_encoding_format_unknown;
}

} // namespace kb::audio_miniaudio
