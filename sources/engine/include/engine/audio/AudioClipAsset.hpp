#pragma once

#include <filesystem>

namespace kb::audio {

struct AudioClipAsset {
    std::filesystem::path path;
};

} // namespace kb::audio
