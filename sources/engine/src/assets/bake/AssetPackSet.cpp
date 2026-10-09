#include "engine/assets/bake/AssetPackSet.hpp"

#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/security/Crypto.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace kb::assets::bake {
namespace {

constexpr std::string_view kHeaderLine = "21kb-pack-set 1";
constexpr std::size_t kMaxPathBytes = 1024U;

[[nodiscard]] std::string LowerAscii(std::string_view text) {
    std::string lower{ text };
    std::ranges::transform(lower, lower.begin(), [](char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
    });
    return lower;
}

[[nodiscard]] std::string_view TakeField(std::string_view& line) noexcept {
    const std::size_t space = line.find(' ');
    const std::string_view field = line.substr(0U, space);
    line = space == std::string_view::npos ? std::string_view{} : line.substr(space + 1U);
    return field;
}

[[nodiscard]] bool ParsePatchLevel(std::string_view text, std::uint32_t& value) noexcept {
    if (text.empty() || (text.size() > 1U && text.front() == '0')) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && value != 0U;
}

} // namespace

std::string_view ToString(AssetPackSetStatus status) noexcept {
    switch (status) {
    case AssetPackSetStatus::Success: return "Success";
    case AssetPackSetStatus::Unreadable: return "Unreadable";
    case AssetPackSetStatus::Malformed: return "Malformed";
    case AssetPackSetStatus::TooLarge: return "TooLarge";
    case AssetPackSetStatus::OrderInvalid: return "OrderInvalid";
    case AssetPackSetStatus::Duplicate: return "Duplicate";
    }
    return "Unknown";
}

bool IsValidAssetPackSetPath(std::string_view path) noexcept {
    if (path.empty() || path.size() > kMaxPathBytes || path.front() == '/' || path.front() == ' ' ||
        path.back() == ' ') {
        return false;
    }
    if (std::ranges::any_of(path, [](char value) {
            return static_cast<unsigned char>(value) < 0x20U || value == '\\' || value == 0x7F || value == ':';
        })) {
        return false;
    }
    std::size_t start = 0U;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view component = path.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        start = end + 1U;
    }
    const std::string lower = LowerAscii(path);
    return lower.size() > kAssetPackFileExtension.size() && lower.ends_with(kAssetPackFileExtension);
}

AssetPackSetStatus ValidateAssetPackSetIndex(const AssetPackSetIndex& index) {
    if (index.packs.size() > kMaxAssetPackSetPacks) {
        return AssetPackSetStatus::TooLarge;
    }
    if (index.packs.empty() || index.packs.front().role != AssetPackRole::Base) {
        return AssetPackSetStatus::OrderInvalid;
    }
    std::set<std::string> paths;
    std::set<std::string> labels;
    bool seenPatch = false;
    std::uint32_t previousPatchLevel = 0U;
    for (std::size_t position = 0U; position < index.packs.size(); ++position) {
        const AssetPackSetEntry& entry = index.packs[position];
        if (!IsValidAssetPackSetPath(entry.path)) {
            return AssetPackSetStatus::Malformed;
        }
        switch (entry.role) {
        case AssetPackRole::Base:
            if (position != 0U) {
                return AssetPackSetStatus::OrderInvalid;
            }
            if (entry.patchLevel != 0U || !entry.label.empty()) {
                return AssetPackSetStatus::Malformed;
            }
            break;
        case AssetPackRole::Chunk:
            if (seenPatch) {
                return AssetPackSetStatus::OrderInvalid;
            }
            if (entry.patchLevel != 0U || !IsValidBakeCacheName(entry.label)) {
                return AssetPackSetStatus::Malformed;
            }
            break;
        case AssetPackRole::Patch:
            if (entry.patchLevel == 0U || !IsValidBakeCacheName(entry.label)) {
                return AssetPackSetStatus::Malformed;
            }
            if (seenPatch && entry.patchLevel <= previousPatchLevel) {
                return AssetPackSetStatus::OrderInvalid;
            }
            seenPatch = true;
            previousPatchLevel = entry.patchLevel;
            break;
        default:
            return AssetPackSetStatus::Malformed;
        }
        // Case-insensitive, because the file systems a game ships on are.
        if (!paths.insert(LowerAscii(entry.path)).second) {
            return AssetPackSetStatus::Duplicate;
        }
        if (!entry.label.empty() && !labels.insert(LowerAscii(entry.label)).second) {
            return AssetPackSetStatus::Duplicate;
        }
    }
    return AssetPackSetStatus::Success;
}

std::string EncodeAssetPackSetIndex(const AssetPackSetIndex& index) {
    if (ValidateAssetPackSetIndex(index) != AssetPackSetStatus::Success) {
        return {};
    }
    std::string text{ kHeaderLine };
    text += '\n';
    for (const AssetPackSetEntry& entry : index.packs) {
        switch (entry.role) {
        case AssetPackRole::Base:
            text += "base " + entry.path;
            break;
        case AssetPackRole::Chunk:
            text += "chunk " + entry.label + ' ' + entry.path;
            break;
        case AssetPackRole::Patch:
            text += "patch " + std::to_string(entry.patchLevel) + ' ' + entry.label + ' ' + entry.path;
            break;
        }
        text += '\n';
    }
    for (const AssetPackSetEntry& entry : index.packs) {
        if (entry.wrappedContentKey.has_value()) {
            text += "key " + kb::security::ToHex(*entry.wrappedContentKey) + ' ' + entry.path + '\n';
        }
    }
    return text;
}

AssetPackSetStatus ParseAssetPackSetIndex(std::string_view text, AssetPackSetIndex& out) {
    if (text.size() > kMaxAssetPackSetFileBytes) {
        return AssetPackSetStatus::TooLarge;
    }
    if (!text.ends_with('\n')) {
        return AssetPackSetStatus::Malformed;
    }
    AssetPackSetIndex index{};
    bool headerSeen = false;
    bool keysSeen = false;
    for (std::string_view rest = text; !rest.empty();) {
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0U, end);
        rest.remove_prefix(end + 1U);
        if (!headerSeen) {
            if (line != kHeaderLine) {
                return AssetPackSetStatus::Malformed;
            }
            headerSeen = true;
            continue;
        }
        if (index.packs.size() == kMaxAssetPackSetPacks) {
            return AssetPackSetStatus::TooLarge;
        }
        AssetPackSetEntry entry{};
        const std::string_view kind = TakeField(line);
        if (kind == "key") {
            // `key <wrapped key> <path>`: the path is the rest of the line and names a listed pack.
            WrappedAssetPackKey wrapped{};
            if (!kb::security::TryParseHex(TakeField(line), wrapped)) {
                return AssetPackSetStatus::Malformed;
            }
            const auto pack = std::ranges::find(index.packs, line, &AssetPackSetEntry::path);
            if (pack == index.packs.end()) {
                return AssetPackSetStatus::Malformed;
            }
            if (pack->wrappedContentKey.has_value()) {
                return AssetPackSetStatus::Duplicate;
            }
            pack->wrappedContentKey = wrapped;
            keysSeen = true;
            continue;
        }
        if (keysSeen) {
            return AssetPackSetStatus::Malformed;
        }
        if (kind == "base") {
            entry.role = AssetPackRole::Base;
        } else if (kind == "chunk") {
            entry.role = AssetPackRole::Chunk;
            entry.label = TakeField(line);
        } else if (kind == "patch") {
            entry.role = AssetPackRole::Patch;
            if (!ParsePatchLevel(TakeField(line), entry.patchLevel)) {
                return AssetPackSetStatus::Malformed;
            }
            entry.label = TakeField(line);
        } else {
            return AssetPackSetStatus::Malformed;
        }
        // The path is the rest of the line, so a path may contain spaces.
        entry.path = line;
        if (!IsValidAssetPackSetPath(entry.path) ||
            (entry.role != AssetPackRole::Base && !IsValidBakeCacheName(entry.label))) {
            return AssetPackSetStatus::Malformed;
        }
        index.packs.push_back(std::move(entry));
    }
    if (!headerSeen) {
        return AssetPackSetStatus::Malformed;
    }
    if (const AssetPackSetStatus status = ValidateAssetPackSetIndex(index); status != AssetPackSetStatus::Success) {
        return status;
    }
    out = std::move(index);
    return AssetPackSetStatus::Success;
}

AssetPackSetStatus ReadAssetPackSetIndex(const std::filesystem::path& path, AssetPackSetIndex& out) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return AssetPackSetStatus::Unreadable;
    }
    if (size > kMaxAssetPackSetFileBytes) {
        return AssetPackSetStatus::TooLarge;
    }
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return AssetPackSetStatus::Unreadable;
    }
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    if (text.size() != size) {
        return AssetPackSetStatus::Unreadable;
    }
    return ParseAssetPackSetIndex(text, out);
}

std::filesystem::path ResolveAssetPackSetPath(const std::filesystem::path& indexPath, std::string_view entryPath) {
    const std::u8string portable{ reinterpret_cast<const char8_t*>(entryPath.data()), entryPath.size() };
    return (indexPath.parent_path() / std::filesystem::path{ portable }).lexically_normal();
}

} // namespace kb::assets::bake
