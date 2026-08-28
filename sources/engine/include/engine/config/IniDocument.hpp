#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace kb::config {

// Sectioned key/value configuration, the format the project's own settings files
// use. Values are stored verbatim and converted on read, so a file written by a
// user keeps whatever they typed until something rewrites that key.
//
// Parsing rules: blank lines and lines whose first non-space character is ';' or
// '#' are ignored, '[Section]' opens a section, and 'key=value' assigns within it.
// Keys and section names are trimmed and matched case-sensitively; a repeated key
// keeps the last assignment. A key before any section header belongs to the empty
// section and is written back without a header.
class IniDocument final {
public:
    // A configuration file that grows past this is treated as corrupt rather than
    // parsed: these are hand-editable settings, not a data channel.
    static constexpr std::size_t MaximumBytes = 256U * 1024U;

    [[nodiscard]] bool Load(const std::filesystem::path& path, std::string& error);
    // Writes through a temporary file and an atomic replace, so an interrupted save
    // cannot leave a half-written configuration behind.
    [[nodiscard]] bool Save(const std::filesystem::path& path, std::string& error) const;

    [[nodiscard]] std::optional<std::string_view> GetString(std::string_view section, std::string_view key) const;
    [[nodiscard]] std::optional<std::int64_t> GetInt(std::string_view section, std::string_view key) const;
    [[nodiscard]] std::optional<bool> GetBool(std::string_view section, std::string_view key) const;

    void SetString(std::string_view section, std::string_view key, std::string value);
    void SetInt(std::string_view section, std::string_view key, std::int64_t value);
    void SetBool(std::string_view section, std::string_view key, bool value);
    // Removes one key, and the section with it when that key was its last.
    bool Remove(std::string_view section, std::string_view key);

    // The entries of one section, in key order. Empty when the section is absent.
    [[nodiscard]] const std::map<std::string, std::string, std::less<>>& SectionEntries(std::string_view section) const;

    [[nodiscard]] bool Empty() const noexcept { return sections_.empty(); }
    void Clear() noexcept { sections_.clear(); }

private:
    using Section = std::map<std::string, std::string, std::less<>>;

    std::map<std::string, Section, std::less<>> sections_;
};

} // namespace kb::config
