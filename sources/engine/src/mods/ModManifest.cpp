#include "engine/mods/ModManifest.hpp"

#include "engine/assets/bake/AssetBakeKey.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <set>

namespace kb::mods {
namespace {

// Keys that would load native code. They are refused by name so the reason is plain.
constexpr std::array<std::string_view, 9U> kNativeKeys{
    "native", "plugin", "plugins", "library", "libraries", "dll", "module", "executable", "binary" };

[[nodiscard]] bool HasControl(std::string_view text) noexcept {
    return std::ranges::any_of(text, [](char value) {
        return static_cast<unsigned char>(value) < 0x20U || static_cast<unsigned char>(value) == 0x7FU;
    });
}

[[nodiscard]] bool IsPlainText(std::string_view text, std::size_t maxBytes) noexcept {
    return !text.empty() && text.size() <= maxBytes && !HasControl(text) && text.front() != ' ' && text.back() != ' ';
}

[[nodiscard]] bool IsValidVirtualPath(std::string_view path) noexcept {
    if (path.size() < 2U || path.size() > 256U || path.front() != '/' || HasControl(path) || path.find('\\') != std::string_view::npos ||
        path.find(' ') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 1U;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view segment = path.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        start = end + 1U;
    }
    return true;
}

struct Line {
    std::uint32_t number = 0U;
    std::string_view key;
    std::string_view value;
};

[[nodiscard]] std::string_view Trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1U);
    }
    return text;
}

ModManifestResult Fail(ModManifestStatus status, std::uint32_t line, std::string detail) {
    return ModManifestResult{ status, line, std::move(detail) };
}

} // namespace

std::string_view ToString(ModManifestStatus status) noexcept {
    switch (status) {
    case ModManifestStatus::Success: return "Success";
    case ModManifestStatus::TooLarge: return "TooLarge";
    case ModManifestStatus::NotAManifest: return "NotAManifest";
    case ModManifestStatus::UnsupportedVersion: return "UnsupportedVersion";
    case ModManifestStatus::Malformed: return "Malformed";
    case ModManifestStatus::UnknownKey: return "UnknownKey";
    case ModManifestStatus::DuplicateKey: return "DuplicateKey";
    case ModManifestStatus::MissingField: return "MissingField";
    case ModManifestStatus::InvalidValue: return "InvalidValue";
    case ModManifestStatus::NativeCodeRefused: return "NativeCodeRefused";
    }
    return "Unknown";
}

std::string ModManifestResult::Describe() const {
    std::string text{ ToString(status) };
    if (line != 0U) {
        text += " (line " + std::to_string(line) + ")";
    }
    if (!detail.empty()) {
        text += ": " + detail;
    }
    return text;
}

bool IsValidModId(std::string_view id) noexcept {
    if (id.size() > kMaxModIdBytes || !kb::assets::bake::IsValidBakeCacheName(id)) {
        return false;
    }
    return std::ranges::none_of(id, [](char value) { return value >= 'A' && value <= 'Z'; });
}

bool IsValidModPackFileName(std::string_view name) noexcept {
    constexpr std::string_view extension = ".kbpack";
    return name.size() > extension.size() && name.size() <= 96U && name.ends_with(extension) &&
        kb::assets::bake::IsValidBakeCacheName(name);
}

ModManifestResult ParseModManifest(std::string_view text, ModManifest& out) {
    if (text.size() > kMaxModManifestBytes) {
        return Fail(ModManifestStatus::TooLarge, 0U, "longer than " + std::to_string(kMaxModManifestBytes) + " bytes");
    }
    ModManifest manifest{};
    std::set<std::string_view> seen;
    bool header = false;
    bool haveVersion = false;
    bool haveApi = false;
    bool havePack = false;
    bool haveHash = false;
    std::uint32_t lineNumber = 0U;
    std::size_t position = 0U;
    while (position <= text.size()) {
        const std::size_t end = std::min(text.find('\n', position), text.size());
        std::string_view line = Trim(text.substr(position, end - position));
        position = end + 1U;
        ++lineNumber;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t space = line.find_first_of(" \t");
        const std::string_view key = line.substr(0U, space);
        const std::string_view value = space == std::string_view::npos ? std::string_view{} : Trim(line.substr(space + 1U));
        if (!header) {
            if (key != kModManifestHeader) {
                return Fail(ModManifestStatus::NotAManifest, lineNumber, "the first line must be \"21kb-mod 1\"");
            }
            if (value != "1") {
                return Fail(ModManifestStatus::UnsupportedVersion, lineNumber, "manifest version " + std::string{ value });
            }
            header = true;
            continue;
        }
        if (std::ranges::find(kNativeKeys, key) != kNativeKeys.end()) {
            return Fail(ModManifestStatus::NativeCodeRefused, lineNumber,
                "a mod is content and scripts only; \"" + std::string{ key } + "\" would load native code");
        }
        const bool repeatable = key == "requires" || key == "conflicts";
        if (!repeatable && !seen.insert(key).second) {
            return Fail(ModManifestStatus::DuplicateKey, lineNumber, std::string{ key });
        }
        if (key == "id") {
            if (!IsValidModId(value)) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "id: lower case letters, digits, '.', '_' and '-', at most 48 characters");
            }
            manifest.id = std::string{ value };
        } else if (key == "name") {
            if (!IsPlainText(value, 64U)) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "name: 1 to 64 printable characters");
            }
            manifest.name = std::string{ value };
        } else if (key == "version") {
            if (!ParseModVersion(value, manifest.version)) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "version: major.minor.patch");
            }
            haveVersion = true;
        } else if (key == "author") {
            if (!IsPlainText(value, 64U)) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "author: 1 to 64 printable characters");
            }
            manifest.author = std::string{ value };
        } else if (key == "description") {
            if (!IsPlainText(value, 512U)) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "description: 1 to 512 printable characters");
            }
            manifest.description = std::string{ value };
        } else if (key == "api") {
            if (!ParseModVersionRange(value, manifest.api)) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "api: a version range");
            }
            haveApi = true;
        } else if (key == "requires") {
            const std::size_t split = value.find(' ');
            const std::string_view id = value.substr(0U, split);
            ModDependency dependency{};
            dependency.id = std::string{ id };
            const std::string_view range = split == std::string_view::npos ? std::string_view{ "*" } : Trim(value.substr(split + 1U));
            if (!IsValidModId(id) || !ParseModVersionRange(range, dependency.range)) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "requires: <mod id> [version range]");
            }
            if (manifest.dependencies.size() >= kMaxModDependencies ||
                std::ranges::any_of(manifest.dependencies, [&](const ModDependency& other) { return other.id == dependency.id; })) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "requires: a mod is listed once, at most 64 mods");
            }
            manifest.dependencies.push_back(std::move(dependency));
        } else if (key == "conflicts") {
            if (!IsValidModId(value) || manifest.conflicts.size() >= kMaxModDependencies ||
                std::ranges::find(manifest.conflicts, std::string{ value }) != manifest.conflicts.end()) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "conflicts: <mod id>, each once");
            }
            manifest.conflicts.emplace_back(value);
        } else if (key == "entry") {
            if (!IsValidVirtualPath(value)) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "entry: a virtual path such as /Game/Mods/Name/Main.21kbscene");
            }
            manifest.entryScene = std::string{ value };
        } else if (key == "pack") {
            if (!IsValidModPackFileName(value)) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "pack: a file name ending in .kbpack, no folders");
            }
            manifest.pack = std::string{ value };
            havePack = true;
        } else if (key == "pack-sha512") {
            if (!kb::security::TryParseHex(value, manifest.packSha512) || value.size() != kb::security::kSha512Bytes * 2U) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "pack-sha512: 128 hexadecimal digits");
            }
            haveHash = true;
        } else if (key == "signer") {
            kb::security::Ed25519PublicKey signer{};
            if (!kb::security::TryParseHex(value, signer) || value.size() != signer.size() * 2U) {
                return Fail(ModManifestStatus::InvalidValue, lineNumber, "signer: 64 hexadecimal digits");
            }
            manifest.signer = signer;
        } else {
            return Fail(ModManifestStatus::UnknownKey, lineNumber, std::string{ key });
        }
    }
    if (!header) {
        return Fail(ModManifestStatus::NotAManifest, 0U, "empty");
    }
    if (manifest.id.empty()) {
        return Fail(ModManifestStatus::MissingField, 0U, "id");
    }
    if (manifest.name.empty()) {
        return Fail(ModManifestStatus::MissingField, 0U, "name");
    }
    if (!haveVersion) {
        return Fail(ModManifestStatus::MissingField, 0U, "version");
    }
    if (!havePack) {
        return Fail(ModManifestStatus::MissingField, 0U, "pack");
    }
    if (!haveHash) {
        return Fail(ModManifestStatus::MissingField, 0U, "pack-sha512");
    }
    if (!haveApi) {
        manifest.api = ModVersionRange{};
    }
    for (const ModDependency& dependency : manifest.dependencies) {
        if (dependency.id == manifest.id) {
            return Fail(ModManifestStatus::InvalidValue, 0U, "a mod cannot require itself");
        }
        if (std::ranges::find(manifest.conflicts, dependency.id) != manifest.conflicts.end()) {
            return Fail(ModManifestStatus::InvalidValue, 0U, "a mod cannot both require and conflict with " + dependency.id);
        }
    }
    if (std::ranges::find(manifest.conflicts, manifest.id) != manifest.conflicts.end()) {
        return Fail(ModManifestStatus::InvalidValue, 0U, "a mod cannot conflict with itself");
    }
    out = std::move(manifest);
    return {};
}

std::string EncodeModManifest(const ModManifest& manifest) {
    std::string text = "21kb-mod 1\n";
    text += "id " + manifest.id + '\n';
    text += "name " + manifest.name + '\n';
    text += "version " + ToString(manifest.version) + '\n';
    if (!manifest.author.empty()) {
        text += "author " + manifest.author + '\n';
    }
    if (!manifest.description.empty()) {
        text += "description " + manifest.description + '\n';
    }
    text += "api " + ToString(manifest.api) + '\n';
    for (const ModDependency& dependency : manifest.dependencies) {
        text += "requires " + dependency.id + ' ' + ToString(dependency.range) + '\n';
    }
    for (const std::string& conflict : manifest.conflicts) {
        text += "conflicts " + conflict + '\n';
    }
    if (!manifest.entryScene.empty()) {
        text += "entry " + manifest.entryScene + '\n';
    }
    text += "pack " + manifest.pack + '\n';
    text += "pack-sha512 " + kb::security::ToHex(manifest.packSha512) + '\n';
    if (manifest.signer.has_value()) {
        text += "signer " + kb::security::ToHex(*manifest.signer) + '\n';
    }
    ModManifest check{};
    return ParseModManifest(text, check).Succeeded() ? text : std::string{};
}

ModManifestResult ReadModManifest(const std::filesystem::path& path, ModManifest& out) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return Fail(ModManifestStatus::NotAManifest, 0U, "cannot read " + path.string());
    }
    if (size > kMaxModManifestBytes) {
        return Fail(ModManifestStatus::TooLarge, 0U, "longer than " + std::to_string(kMaxModManifestBytes) + " bytes");
    }
    std::ifstream input{ path, std::ios::binary };
    const std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
    if (!input && !input.eof()) {
        return Fail(ModManifestStatus::NotAManifest, 0U, "cannot read " + path.string());
    }
    return ParseModManifest(text, out);
}

} // namespace kb::mods
