#include "kb/packages/PackageArchive.hpp"

#include "PackagesInternal.hpp"

#include "kb/packages/Packages.hpp"

#include "engine/assets/AssetFileName.hpp"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

namespace kb::packages {
namespace {

using namespace detail;

constexpr std::size_t kBlock = 512U;
constexpr int kCompressionLevel = 9;

// ustar header fields.
constexpr std::size_t kNameAt = 0U;
constexpr std::size_t kNameLength = 100U;
constexpr std::size_t kSizeAt = 124U;
constexpr std::size_t kChecksumAt = 148U;
constexpr std::size_t kTypeAt = 156U;
constexpr std::size_t kMagicAt = 257U;
constexpr std::size_t kPrefixAt = 345U;
constexpr std::size_t kPrefixLength = 155U;

[[nodiscard]] bool IsSafeComponent(std::string_view part) {
    if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') {
        return false;
    }
    for (const char c : part) {
        if (static_cast<unsigned char>(c) < 0x20U || std::string_view{ "\\:*?\"<>|" }.find(c) != std::string_view::npos) {
            return false;
        }
    }
    // Names Windows reserves for devices, with or without an extension.
    std::string stem{ part.substr(0U, part.find('.')) };
    std::ranges::transform(stem, stem.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const bool reserved = stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        ((stem.starts_with("COM") || stem.starts_with("LPT")) && stem.size() == 4U && stem[3] >= '1' && stem[3] <= '9');
    return !reserved;
}

// A path below the package folder: parts separated by '/', none empty, none climbing, nothing a system would read as
// a drive, a device or another name.
[[nodiscard]] bool IsSafePath(std::string_view path) {
    if (path.empty() || path.size() > kPrefixLength + 1U + kNameLength) {
        return false;
    }
    for (std::size_t start = 0U;;) {
        const std::size_t end = path.find('/', start);
        if (!IsSafeComponent(path.substr(start, end == std::string_view::npos ? end : end - start))) {
            return false;
        }
        if (end == std::string_view::npos) {
            return true;
        }
        start = end + 1U;
    }
}

void PutOctal(char* field, std::size_t width, std::uint64_t value) {
    for (std::size_t digit = width - 1U; digit-- > 0U; value >>= 3U) {
        field[digit] = static_cast<char>('0' + (value & 7U));
    }
    field[width - 1U] = '\0';
}

[[nodiscard]] bool ParseOctal(const char* field, std::size_t width, std::uint64_t& value) {
    value = 0U;
    std::size_t digits = 0U;
    for (std::size_t index = 0U; index < width && field[index] != '\0' && field[index] != ' '; ++index, ++digits) {
        if (field[index] < '0' || field[index] > '7' || digits >= 12U) {
            return false;
        }
        value = (value << 3U) | static_cast<std::uint64_t>(field[index] - '0');
    }
    return digits != 0U;
}

[[nodiscard]] std::uint32_t HeaderSum(const char* header) {
    std::uint32_t sum = 0U;
    for (std::size_t index = 0U; index < kBlock; ++index) {
        // The checksum field counts as spaces.
        sum += (index >= kChecksumAt && index < kChecksumAt + 8U) ? 32U : static_cast<unsigned char>(header[index]);
    }
    return sum;
}

// The tar header of one entry; false when the path does not fit the name and prefix fields.
[[nodiscard]] bool MakeHeader(std::string_view path, std::uint64_t size, char type, std::array<char, kBlock>& header) {
    header.fill('\0');
    std::string_view prefix;
    std::string_view name = path;
    if (path.size() > kNameLength) {
        const std::size_t first = path.size() - kNameLength - 1U;
        const std::size_t split = path.find('/', first);
        if (split == std::string_view::npos || split > kPrefixLength) {
            return false;
        }
        prefix = path.substr(0U, split);
        name = path.substr(split + 1U);
    }
    std::memcpy(header.data() + kNameAt, name.data(), name.size());
    std::memcpy(header.data() + kPrefixAt, prefix.data(), prefix.size());
    PutOctal(header.data() + 100U, 8U, type == '5' ? 0755U : 0644U);
    PutOctal(header.data() + 108U, 8U, 0U);
    PutOctal(header.data() + 116U, 8U, 0U);
    PutOctal(header.data() + kSizeAt, 12U, size);
    PutOctal(header.data() + 136U, 12U, 0U);
    header[kTypeAt] = type;
    std::memcpy(header.data() + kMagicAt, "ustar\0" "00", 8U);
    PutOctal(header.data() + kChecksumAt, 7U, HeaderSum(header.data()));
    header[kChecksumAt + 7U] = ' ';
    return true;
}

void Pad(std::vector<std::uint8_t>& tar) {
    tar.resize((tar.size() + kBlock - 1U) / kBlock * kBlock, 0U);
}

void EndTar(std::vector<std::uint8_t>& tar) {
    tar.resize(tar.size() + 2U * kBlock, 0U);
}

[[nodiscard]] bool Compress(const std::vector<std::uint8_t>& tar, std::vector<std::uint8_t>& archive, std::string& error) {
    archive.assign(ZSTD_compressBound(tar.size()), 0U);
    // ZSTD_compress puts the content size into the frame, so a reader can refuse an archive that unpacks to too much.
    const std::size_t written = ZSTD_compress(archive.data(), archive.size(), tar.data(), tar.size(), kCompressionLevel);
    if (ZSTD_isError(written) != 0U || written > kMaxArchiveBytes) {
        error = ZSTD_isError(written) != 0U ? "the archive could not be compressed" : "the archive is larger than " + std::to_string(kMaxArchiveBytes) + " bytes";
        return false;
    }
    archive.resize(written);
    return true;
}

struct Member {
    std::string path;
    std::size_t offset = 0U;
    std::size_t size = 0U;
    bool folder = false;
};

// Reads every header of a tar stream and checks it; nothing is written.
[[nodiscard]] bool ListMembers(const std::vector<std::uint8_t>& tar, std::vector<Member>& members, std::string& error) {
    const auto refuse = [&error](const std::string& why) {
        error = "The package archive is refused: " + why + ".";
        return false;
    };
    std::set<std::string> seen;
    for (std::size_t at = 0U;;) {
        if (at + kBlock > tar.size()) {
            return refuse("it ends inside an entry");
        }
        const char* header = reinterpret_cast<const char*>(tar.data() + at);
        if (std::all_of(header, header + kBlock, [](char c) { return c == '\0'; })) {
            break;
        }
        std::uint64_t stored = 0U;
        if (!ParseOctal(header + kChecksumAt, 8U, stored) || stored != HeaderSum(header) || std::memcmp(header + kMagicAt, "ustar", 5U) != 0) {
            return refuse("an entry header is damaged");
        }
        const char type = header[kTypeAt] == '\0' ? '0' : header[kTypeAt];
        if (type != '0' && type != '5') {
            return refuse("it holds a link or another special entry, and packages hold plain files only");
        }
        std::uint64_t size = 0U;
        if (!ParseOctal(header + kSizeAt, 12U, size) || (type == '5' && size != 0U) || size > tar.size()) {
            return refuse("an entry has an invalid size");
        }
        std::string path{ header + kPrefixAt, strnlen(header + kPrefixAt, kPrefixLength) };
        path += path.empty() ? "" : "/";
        path.append(header + kNameAt, strnlen(header + kNameAt, kNameLength));
        if (type == '5' && path.ends_with('/')) {
            path.pop_back();
        }
        if (!IsSafePath(path)) {
            return refuse("the path '" + path + "' is not allowed (it is absolute, climbs out of the package folder, or is not a plain name)");
        }
        std::string key = path;
        std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!seen.insert(key).second) {
            return refuse("the path '" + path + "' appears twice");
        }
        const std::size_t data = at + kBlock;
        const std::size_t padded = (static_cast<std::size_t>(size) + kBlock - 1U) / kBlock * kBlock;
        if (data + padded > tar.size()) {
            return refuse("it ends inside the file '" + path + "'");
        }
        if (members.size() >= kMaxArchiveFiles) {
            return refuse("it holds too many files");
        }
        members.push_back({ path, data, static_cast<std::size_t>(size), type == '5' });
        at = data + padded;
    }
    if (std::ranges::none_of(members, [](const Member& member) { return !member.folder && member.path == kManifestFile; })) {
        return refuse(std::string{ "it has no " } + std::string{ kManifestFile } + " at its top");
    }
    return true;
}

} // namespace

bool PackEntries(const std::vector<ArchiveEntry>& entries, std::vector<std::uint8_t>& archive, std::string& error) {
    std::vector<std::uint8_t> tar;
    for (const ArchiveEntry& entry : entries) {
        std::array<char, kBlock> header{};
        if (!MakeHeader(entry.path, entry.content.size(), entry.type, header)) {
            error = "the path '" + entry.path + "' is too long for an archive";
            return false;
        }
        tar.insert(tar.end(), header.begin(), header.end());
        tar.insert(tar.end(), entry.content.begin(), entry.content.end());
        Pad(tar);
    }
    EndTar(tar);
    return Compress(tar, archive, error);
}

bool BuildArchive(const fs::path& folder, std::vector<std::uint8_t>& archive, std::string& error) {
    std::map<std::string, std::uintmax_t> sizes;
    if (!ListFiles(folder, sizes, error)) {
        return false;
    }
    std::vector<std::uint8_t> tar;
    for (const auto& [path, size] : sizes) {
        std::array<char, kBlock> header{};
        if (!IsSafePath(path) || !MakeHeader(path, size, '0', header)) {
            error = "the file '" + path + "' cannot be packed: its name is too long or not usable on every system";
            return false;
        }
        if (tar.size() + kBlock + size > kMaxUnpackedBytes) {
            error = "the package is larger than " + std::to_string(kMaxUnpackedBytes) + " bytes";
            return false;
        }
        tar.insert(tar.end(), header.begin(), header.end());
        const std::size_t at = tar.size();
        tar.resize(at + static_cast<std::size_t>(size));
        std::ifstream input{ folder / kb::assets::Utf8Path(path), std::ios::binary };
        input.read(reinterpret_cast<char*>(tar.data() + at), static_cast<std::streamsize>(size));
        if (!input && size != 0U) {
            error = "cannot read " + Utf8Text(folder / kb::assets::Utf8Path(path));
            return false;
        }
        Pad(tar);
    }
    EndTar(tar);
    return Compress(tar, archive, error);
}

bool ExtractArchive(std::span<const std::uint8_t> archive, const fs::path& destination, std::string& error) {
    const unsigned long long declared = ZSTD_getFrameContentSize(archive.data(), archive.size());
    if (archive.empty() || declared == ZSTD_CONTENTSIZE_UNKNOWN || declared == ZSTD_CONTENTSIZE_ERROR || declared > kMaxUnpackedBytes ||
        ZSTD_findFrameCompressedSize(archive.data(), archive.size()) != archive.size()) {
        error = "The package archive is refused: it is not a package archive, or it unpacks to more than " + std::to_string(kMaxUnpackedBytes) + " bytes.";
        return false;
    }
    std::vector<std::uint8_t> tar(static_cast<std::size_t>(declared));
    if (ZSTD_isError(ZSTD_decompress(tar.data(), tar.size(), archive.data(), archive.size())) != 0U) {
        error = "The package archive is refused: it is damaged.";
        return false;
    }
    std::vector<Member> members;
    if (!ListMembers(tar, members, error)) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(destination, ec);
    for (const Member& member : members) {
        const fs::path target = destination / kb::assets::Utf8Path(member.path);
        if (member.folder) {
            fs::create_directories(target, ec);
        } else {
            fs::create_directories(target.parent_path(), ec);
            std::ofstream output{ target, std::ios::binary | std::ios::trunc };
            output.write(reinterpret_cast<const char*>(tar.data() + member.offset), static_cast<std::streamsize>(member.size));
            output.flush();
            if (!output) {
                error = "cannot write " + Utf8Text(target);
                return false;
            }
        }
        if (ec) {
            error = "cannot write " + Utf8Text(target) + ": " + ec.message();
            return false;
        }
    }
    return true;
}

} // namespace kb::packages
