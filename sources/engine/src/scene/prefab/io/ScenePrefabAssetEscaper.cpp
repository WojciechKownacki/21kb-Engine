#include "scene/prefab/io/ScenePrefabAssetEscaper.hpp"

namespace kb::scene {

std::string ScenePrefabAssetEscaper::Escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

std::optional<std::string> ScenePrefabAssetEscaper::Unescape(std::string_view value) {
    std::string unescaped;
    unescaped.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character != '\\') {
            unescaped.push_back(character);
            continue;
        }

        if (++index >= value.size()) {
            return std::nullopt;
        }

        switch (value[index]) {
        case '\\':
            unescaped.push_back('\\');
            break;
        case 'n':
            unescaped.push_back('\n');
            break;
        case 'r':
            unescaped.push_back('\r');
            break;
        case 't':
            unescaped.push_back('\t');
            break;
        default:
            return std::nullopt;
        }
    }
    return unescaped;
}

std::string ScenePrefabAssetEscaper::EncodeHex(std::span<const std::uint8_t> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2U);
    for (const std::uint8_t byte : bytes) {
        text.push_back(digits[byte >> 4U]);
        text.push_back(digits[byte & 0x0FU]);
    }
    return text;
}

std::optional<std::vector<std::uint8_t>> ScenePrefabAssetEscaper::DecodeHex(std::string_view text) {
    const auto digit = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        return -1;
    };
    if (text.size() % 2U != 0U) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 2U);
    for (std::size_t index = 0U; index < text.size(); index += 2U) {
        const int high = digit(text[index]);
        const int low = digit(text[index + 1U]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::uint8_t>(high << 4 | low));
    }
    return bytes;
}

} // namespace kb::scene
