#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::audio {

struct AudioClipAsset {
    std::filesystem::path path;
    std::vector<std::uint8_t> encodedBytes;
    std::string sourceExtension;

    [[nodiscard]] bool IsMemoryBacked() const noexcept {
        return !encodedBytes.empty();
    }
};

} // namespace kb::audio
