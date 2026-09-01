#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace kb::editor::package_input {

[[nodiscard]] inline bool IsPathInsideOrEqual(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) noexcept {
    std::error_code error;
    const std::filesystem::path resolvedRoot = std::filesystem::weakly_canonical(root, error);
    if (error || resolvedRoot.empty()) return false;
    const std::filesystem::path resolvedCandidate = std::filesystem::weakly_canonical(candidate, error);
    if (error || resolvedCandidate.empty()) return false;
    const std::filesystem::path relative = std::filesystem::relative(resolvedCandidate, resolvedRoot, error);
    if (error || relative.is_absolute()) return false;
    if (relative.empty() || relative == ".") return true;
    for (const std::filesystem::path& part : relative) {
        if (part == "..") return false;
    }
    return true;
}

[[nodiscard]] inline bool IsNormalProjectRelativePath(const std::filesystem::path& value) noexcept {
    if (value.empty() || value.is_absolute() || value.has_root_name() || value.has_root_directory() ||
        value.lexically_normal() != value) return false;
    for (const std::filesystem::path& part : value) {
        if (part.empty() || part == "." || part == "..") return false;
    }
    return true;
}

[[nodiscard]] inline bool IsValidProjectPngIcon(
    const std::filesystem::path& projectFile,
    const std::filesystem::path& icon) noexcept {
    if (!projectFile.is_absolute() || !icon.is_absolute()) return false;
    std::error_code error;
    const std::filesystem::path projectRoot = std::filesystem::weakly_canonical(projectFile.parent_path(), error);
    if (error || projectRoot.empty()) return false;
    const std::filesystem::path resolvedIcon = std::filesystem::canonical(icon, error);
    if (error || resolvedIcon.empty() || !std::filesystem::is_regular_file(resolvedIcon, error) || error) return false;
    if (!IsPathInsideOrEqual(projectRoot, resolvedIcon) || resolvedIcon == projectRoot) return false;
    std::string extension = resolvedIcon.extension().string();
    for (char& character : extension) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    if (extension != ".png") return false;
    constexpr std::array<unsigned char, 8> expected{ 0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU, 0x0aU, 0x1aU, 0x0aU };
    std::array<unsigned char, expected.size()> signature{};
    std::ifstream stream{ resolvedIcon, std::ios::binary };
    stream.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
    return stream && signature == expected;
}

[[nodiscard]] inline bool IsAsciiAlphaNumeric(char character) noexcept {
    return (character >= 'A' && character <= 'Z') ||
        (character >= 'a' && character <= 'z') ||
        (character >= '0' && character <= '9');
}

[[nodiscard]] inline bool AsciiEqualsIgnoreCase(
    std::string_view left,
    std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto lower = [](char character) noexcept {
            return character >= 'A' && character <= 'Z'
                ? static_cast<char>(character + ('a' - 'A')) : character;
        };
        if (lower(left[index]) != lower(right[index])) return false;
    }
    return true;
}

[[nodiscard]] inline bool IsWindowsReservedDeviceName(std::string_view value) noexcept {
    const std::string_view base = value.substr(0U, value.find('.'));
    if (AsciiEqualsIgnoreCase(base, "CON") || AsciiEqualsIgnoreCase(base, "PRN") ||
        AsciiEqualsIgnoreCase(base, "AUX") || AsciiEqualsIgnoreCase(base, "NUL") ||
        AsciiEqualsIgnoreCase(base, "CONIN$") || AsciiEqualsIgnoreCase(base, "CONOUT$")) {
        return true;
    }
    return base.size() == 4U && base.back() >= '1' && base.back() <= '9' &&
        (AsciiEqualsIgnoreCase(base.substr(0U, 3U), "COM") ||
            AsciiEqualsIgnoreCase(base.substr(0U, 3U), "LPT"));
}

[[nodiscard]] inline bool IsValidPackageName(
    std::string_view value,
    bool windowsTarget) noexcept {
    if (value.empty() || value.size() > 80U || value.back() == '.' || value.back() == ' ' ||
        !IsAsciiAlphaNumeric(value.front())) {
        return false;
    }
    for (const char character : value) {
        if (!IsAsciiAlphaNumeric(character) && character != '_' && character != '.' &&
            character != '-' && character != ' ') {
            return false;
        }
    }
    return !windowsTarget || !IsWindowsReservedDeviceName(value);
}

[[nodiscard]] inline bool IsValidAndroidKeyAlias(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U) return false;
    for (const char character : value) {
        if (!IsAsciiAlphaNumeric(character) && character != '_' &&
            character != '-' && character != '.') return false;
    }
    return true;
}

[[nodiscard]] inline bool IsValidLinuxHost(std::string_view value) noexcept {
    if (value.empty() || value.size() > 253U) return false;
    for (const char character : value) {
        if (!IsAsciiAlphaNumeric(character) && character != '.' && character != '-') return false;
    }
    return true;
}

[[nodiscard]] inline bool IsValidLinuxUser(std::string_view value) noexcept {
    if (value.empty() || value.size() > 32U ||
        !((value.front() >= 'A' && value.front() <= 'Z') ||
          (value.front() >= 'a' && value.front() <= 'z') || value.front() == '_')) return false;
    for (const char character : value.substr(1U)) {
        if (!IsAsciiAlphaNumeric(character) && character != '_' && character != '-') return false;
    }
    return true;
}

[[nodiscard]] inline bool IsValidLinuxHostKey(std::string_view value) {
    std::istringstream input{ std::string{ value } };
    std::string type;
    std::string key;
    std::string extra;
    if (!(input >> type >> key) || (input >> extra)) return false;
    if (type != "ssh-ed25519" && type != "ecdsa-sha2-nistp256" && type != "rsa-sha2-512") return false;
    if (key.empty()) return false;
    for (const char character : key) {
        if (!IsAsciiAlphaNumeric(character) && character != '+' && character != '/' && character != '=') return false;
    }
    return true;
}

[[nodiscard]] inline bool IsValidLinuxEngineRoot(std::string_view value) noexcept {
    if (value.empty() || value.front() != '/') return false;
    for (const char character : value) {
        if (!IsAsciiAlphaNumeric(character) && character != '_' && character != '.' &&
            character != '/' && character != '-') return false;
    }
    std::size_t begin = 0U;
    while (begin < value.size()) {
        const std::size_t end = value.find('/', begin);
        const std::string_view part = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (part == "." || part == "..") return false;
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return true;
}

[[nodiscard]] inline bool IsValidLinuxDisplay(std::string_view value) noexcept {
    if (value.empty() || value.front() != ':') return false;
    value.remove_prefix(1U);
    const auto digits = [](std::string_view part) noexcept {
        if (part.empty() || part.size() > 5U) return false;
        for (const char character : part) {
            if (character < '0' || character > '9') return false;
        }
        return true;
    };
    const std::size_t dot = value.find('.');
    return dot == std::string_view::npos
        ? digits(value)
        : value.find('.', dot + 1U) == std::string_view::npos &&
            digits(value.substr(0U, dot)) && digits(value.substr(dot + 1U));
}

} // namespace kb::editor::package_input
