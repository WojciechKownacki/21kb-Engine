#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class ScenePrefabAssetEscaper {
public:
    ScenePrefabAssetEscaper() = delete;

    [[nodiscard]] static std::string Escape(std::string_view value);
    [[nodiscard]] static std::optional<std::string> Unescape(std::string_view value);
    // Bytes as lower case hexadecimal digits, and back.
    [[nodiscard]] static std::string EncodeHex(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> DecodeHex(std::string_view text);
};

} // namespace kb::scene
