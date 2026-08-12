#pragma once

#include <cstdint>

namespace kb::audio {

enum class AudioAttenuationModel : std::int32_t {
    None = 0,
    Inverse = 1,
    Linear = 2,
    Exponential = 3,
};

[[nodiscard]] inline constexpr bool IsAudioAttenuationModelValid(AudioAttenuationModel model) noexcept {
    switch (model) {
    case AudioAttenuationModel::None:
    case AudioAttenuationModel::Inverse:
    case AudioAttenuationModel::Linear:
    case AudioAttenuationModel::Exponential:
        return true;
    }
    return false;
}

} // namespace kb::audio
