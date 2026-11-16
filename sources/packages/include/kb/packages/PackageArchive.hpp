#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

// The archive of a package: a tar stream (plain files only, paths below one folder) compressed as one zstd frame. The
// archive of the same folder is always the same bytes, so its sum can be published.
namespace kb::packages {

// What unpacking may produce: more is refused before any memory is spent on it.
inline constexpr std::uint64_t kMaxUnpackedBytes = 1024ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t kMaxArchiveFiles = 1'000'000U;

// The archive of the files of a folder (see ListFiles). Fails on a file name that cannot be unpacked safely on every
// system (reserved names, characters, trailing dots) and on a folder over kMaxUnpackedBytes.
[[nodiscard]] bool BuildArchive(const std::filesystem::path& folder, std::vector<std::uint8_t>& archive, std::string& error);

// Unpacks into `destination`, which is made. Everything is checked before the first file is written: a path that is
// absolute, climbs out with "..", names a drive, repeats another, or an entry that is not a plain file or a folder
// (links, devices) refuses the whole archive. An archive needs a package.json at its top.
[[nodiscard]] bool ExtractArchive(std::span<const std::uint8_t> archive, const std::filesystem::path& destination, std::string& error);

// For tests of ExtractArchive: entries written as given. `type` is the tar type flag ('0' file, '5' folder, '2' link).
struct ArchiveEntry {
    std::string path;
    std::string content;
    char type = '0';
};
[[nodiscard]] bool PackEntries(const std::vector<ArchiveEntry>& entries, std::vector<std::uint8_t>& archive, std::string& error);

} // namespace kb::packages
