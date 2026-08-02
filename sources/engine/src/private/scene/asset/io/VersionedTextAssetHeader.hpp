#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace kb::scene::asset_io {

enum class TextAssetHeaderStatus : std::uint8_t {
    Legacy,
    Current,
    Invalid,
};

[[nodiscard]] inline TextAssetHeaderStatus ParseTextAssetHeader(
    std::string_view line,
    std::string_view expectedType,
    std::uint32_t currentSchemaVersion) {
    constexpr std::string_view kMagic = "21kb";
    if (!line.starts_with(kMagic)) return TextAssetHeaderStatus::Legacy;

    std::istringstream input{ std::string{ line } };
    std::string magic;
    std::string type;
    std::uint32_t version = 0U;
    if (!(input >> magic >> type >> version) || magic != kMagic ||
        type != expectedType) {
        return TextAssetHeaderStatus::Invalid;
    }
    input >> std::ws;
    if (!input.eof() || version != currentSchemaVersion) {
        return TextAssetHeaderStatus::Invalid;
    }
    return TextAssetHeaderStatus::Current;
}

[[nodiscard]] inline std::string TextAssetHeader(
    std::string_view type,
    std::uint32_t schemaVersion) {
    return "21kb " + std::string{ type } + ' ' + std::to_string(schemaVersion) + '\n';
}

} // namespace kb::scene::asset_io
