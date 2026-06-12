#pragma once

#include <cstdint>

namespace kb::audio {

enum class AudioAttenuationModel : std::int32_t {
    None = 0,
    Inverse = 1,
    Linear = 2,
    Exponential = 3,
};

} // namespace kb::audio
