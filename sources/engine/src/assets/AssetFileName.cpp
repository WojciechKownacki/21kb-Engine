#include "engine/assets/AssetFileName.hpp"

#include <algorithm>
#include <array>

namespace kb::assets {
namespace {

[[nodiscard]] bool IsReservedDeviceName(std::string_view name) noexcept {
    static constexpr std::array<std::string_view, 22> kReserved{
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    std::string stem{ name.substr(0, name.find('.')) };
    std::ranges::transform(stem, stem.begin(), [](unsigned char character) {
        return static_cast<char>(character >= 'a' && character <= 'z' ? character - 'a' + 'A' : character);
    });
    return std::ranges::find(kReserved, std::string_view{ stem }) != kReserved.end();
}

} // namespace

std::string AssetFileNameError(std::string_view name) {
    if (name.empty()) {
        return "Enter a name.";
    }
    if (name.size() > 255U) {
        return "The name is too long (255 characters at most).";
    }
    for (const char character : name) {
        if (character == '/' || character == '\\') {
            return "A name can't contain / or \\.";
        }
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U) {
            return "A name can't contain control characters.";
        }
        if (character == '<' || character == '>' || character == ':' || character == '"' || character == '|' ||
            character == '?' || character == '*') {
            return std::string{ "A name can't contain the character '" } + character + "'.";
        }
    }
    if (name == "." || name == "..") {
        return "'.' and '..' are reserved names.";
    }
    if (IsReservedDeviceName(name)) {
        return "'" + std::string{ name.substr(0, name.find('.')) } + "' is a name Windows reserves for devices.";
    }
    if (name.back() == ' ' || name.back() == '.') {
        return "A name can't end with a space or a dot.";
    }
    return {};
}

} // namespace kb::assets
