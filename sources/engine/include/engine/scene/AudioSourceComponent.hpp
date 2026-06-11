#pragma once

#include <cstdint>

namespace kb::scene {

struct AudioSourceComponent {
    std::uint64_t clipAssetId = 0;
    float volume = 1.0F;
    float pitch = 1.0F;
    bool loop = false;
    bool spatial = true;
    bool autoplay = false;
};

} // namespace kb::scene
