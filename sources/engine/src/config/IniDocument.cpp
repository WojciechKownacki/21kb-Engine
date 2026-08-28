#include "engine/config/IniDocument.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <locale>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::config {
namespace {

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] bool IsCommentOrBlank(std::string_view line) noexcept {
    const std::string_view trimmed = Trim(line);
    return trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#';
}

[[nodiscard]] bool Replace(const std::filesystem::path& source, const std::filesystem::path& target) noexcept {
#if defined(_WIN32)
    return MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(source, target, error);
    return !error;
#endif
}

} // namespace

bool IniDocument::Load(const std::filesystem::path& path, std::string& error) {
    error.clear();
    sections_.clear();

    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError) || filesystemError) {
        error = "Configuration file does not exist.";
        return false;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, filesystemError);
    if (filesystemError) {
        error = "Configuration file size could not be read.";
        return false;
    }
    if (size > MaximumBytes) {
        error = "Configuration file exceeds its bounded size.";
        return false;
    }

    std::ifstream input{path, std::ios::binary};
    if (!input) {
        error = "Configuration file could not be opened.";
        return false;
    }

    std::string line;
    std::string current;
    std::size_t lineNumber = 0U;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (IsCommentOrBlank(line)) {
            continue;
        }

        const std::string_view trimmed = Trim(line);
        if (trimmed.front() == '[') {
            if (trimmed.back() != ']' || trimmed.size() <= 2U) {
                error = "Configuration line " + std::to_string(lineNumber) + " has a malformed section header.";
                sections_.clear();
                return false;
            }
            current = std::string{Trim(trimmed.substr(1U, trimmed.size() - 2U))};
            static_cast<void>(sections_[current]);
            continue;
        }

        const std::size_t separator = trimmed.find('=');
        if (separator == std::string_view::npos) {
            error = "Configuration line " + std::to_string(lineNumber) + " is neither a section nor an assignment.";
            sections_.clear();
            return false;
        }
        const std::string_view key = Trim(trimmed.substr(0U, separator));
        if (key.empty()) {
            error = "Configuration line " + std::to_string(lineNumber) + " assigns to an empty key.";
            sections_.clear();
            return false;
        }
        sections_[current][std::string{key}] = std::string{Trim(trimmed.substr(separator + 1U))};
    }
    return true;
}

bool IniDocument::Save(const std::filesystem::path& path, std::string& error) const {
    error.clear();

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    for (const auto& [section, values] : sections_) {
        if (!section.empty()) {
            stream << '[' << section << "]\n";
        }
        for (const auto& [key, value] : values) {
            stream << key << '=' << value << '\n';
        }
        stream << '\n';
    }

    const std::string bytes = stream.str();
    if (bytes.size() > MaximumBytes) {
        error = "Configuration exceeds its bounded size.";
        return false;
    }

    std::error_code filesystemError;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError) {
            error = "Configuration directory could not be created.";
            return false;
        }
    }

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, filesystemError);
            error = "Configuration temporary file could not be written.";
            return false;
        }
    }
    if (!Replace(temporary, path)) {
        std::filesystem::remove(temporary, filesystemError);
        error = "Configuration atomic replacement failed.";
        return false;
    }
    return true;
}

std::optional<std::string_view> IniDocument::GetString(std::string_view section, std::string_view key) const {
    const auto sectionEntry = sections_.find(section);
    if (sectionEntry == sections_.end()) {
        return std::nullopt;
    }
    const auto valueEntry = sectionEntry->second.find(key);
    if (valueEntry == sectionEntry->second.end()) {
        return std::nullopt;
    }
    return std::string_view{valueEntry->second};
}

std::optional<std::int64_t> IniDocument::GetInt(std::string_view section, std::string_view key) const {
    const std::optional<std::string_view> text = GetString(section, key);
    if (!text.has_value() || text->empty()) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const char* begin = text->data();
    const char* end = text->data() + text->size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> IniDocument::GetBool(std::string_view section, std::string_view key) const {
    const std::optional<std::string_view> text = GetString(section, key);
    if (!text.has_value()) {
        return std::nullopt;
    }
    std::string lowered{*text};
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        return false;
    }
    return std::nullopt;
}

void IniDocument::SetString(std::string_view section, std::string_view key, std::string value) {
    sections_[std::string{section}][std::string{key}] = std::move(value);
}

void IniDocument::SetInt(std::string_view section, std::string_view key, std::int64_t value) {
    SetString(section, key, std::to_string(value));
}

void IniDocument::SetBool(std::string_view section, std::string_view key, bool value) {
    SetString(section, key, value ? "1" : "0");
}

bool IniDocument::Remove(std::string_view section, std::string_view key) {
    const auto sectionEntry = sections_.find(section);
    if (sectionEntry == sections_.end()) {
        return false;
    }
    const auto valueEntry = sectionEntry->second.find(key);
    if (valueEntry == sectionEntry->second.end()) {
        return false;
    }
    sectionEntry->second.erase(valueEntry);
    if (sectionEntry->second.empty()) {
        sections_.erase(sectionEntry);
    }
    return true;
}

} // namespace kb::config
