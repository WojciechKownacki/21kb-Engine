#include "engine/security/ReleaseKeys.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <system_error>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

namespace kb::security {
namespace {

constexpr std::string_view kSigningKeyHeader = "21kb-release-signing-key 1";
constexpr std::string_view kContentKeyHeader = "21kb-pack-content-key 1";
constexpr std::array<std::uint8_t, 8U> kAnchorMagic{ '2', '1', 'K', 'B', 'T', 'R', 'S', 'T' };
constexpr std::uint32_t kAnchorVersion = 1U;
constexpr std::uint32_t kAnchorHasContentKey = 1U << 0U;
constexpr std::uint32_t kAnchorKnownFlags = kAnchorHasContentKey;
// magic, version, flags, release key, content key, product id length
constexpr std::size_t kAnchorFixedBytes = 8U + 4U + 4U + 32U + 32U + 2U;

// Splits "line\nline\n" (or CRLF) into lines; a final newline is optional, blank lines are not.
[[nodiscard]] std::vector<std::string_view> Lines(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0U, end);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        lines.push_back(line);
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1U);
    }
    return lines;
}

[[nodiscard]] bool ReadHexField(std::string_view line, std::string_view name, std::span<std::uint8_t> out) {
    if (line.size() != name.size() + 1U + out.size() * 2U || !line.starts_with(name) || line[name.size()] != ' ') {
        return false;
    }
    return TryParseHex(line.substr(name.size() + 1U), out);
}

void PutUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] std::uint32_t GetUInt32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

#if defined(_WIN32)
[[nodiscard]] TrustAnchorLookup LookupResource(HMODULE module) {
    TrustAnchorLookup lookup{};
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(kTrustAnchorResourceId), MAKEINTRESOURCEW(10));
    if (resource == nullptr) {
        return lookup;
    }
    lookup.state = TrustAnchorLookup::State::Invalid;
    const DWORD size = SizeofResource(module, resource);
    HGLOBAL loaded = LoadResource(module, resource);
    const void* data = loaded == nullptr ? nullptr : LockResource(loaded);
    if (data == nullptr || size == 0U) {
        lookup.error = "the embedded trust anchor resource could not be read";
        return lookup;
    }
    if (DecodeTrustAnchor(std::span{ static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size) },
            lookup.anchor, lookup.error)) {
        lookup.state = TrustAnchorLookup::State::Present;
    }
    return lookup;
}
#endif

} // namespace

bool IsValidProductId(std::string_view productId) noexcept {
    if (productId.empty() || productId.size() > kMaxProductIdBytes) {
        return false;
    }
    const auto alphanumeric = [](char value) noexcept {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9');
    };
    return alphanumeric(productId.front()) && std::ranges::all_of(productId, [&](char value) {
        return alphanumeric(value) || value == '.' || value == '_' || value == '-';
    });
}

bool GenerateReleaseSigningKey(ReleaseSigningKey& out) noexcept {
    if (!GenerateEd25519Seed(out.seed)) {
        return false;
    }
    Ed25519KeyPairFromSeed(out.seed, out.secretKey, out.publicKey);
    return true;
}

std::string EncodeReleaseSigningKey(const ReleaseSigningKey& key) {
    std::string text{ kSigningKeyHeader };
    text += "\nseed ";
    text += ToHex(key.seed.Span());
    text += "\npublic ";
    text += ToHex(key.publicKey);
    text += '\n';
    return text;
}

bool DecodeReleaseSigningKey(std::string_view text, ReleaseSigningKey& out, std::string& error) {
    const std::vector<std::string_view> lines = Lines(text);
    Ed25519PublicKey recordedPublicKey{};
    if (lines.size() != 3U || lines[0] != kSigningKeyHeader || !ReadHexField(lines[1], "seed", out.seed.Span()) ||
        !ReadHexField(lines[2], "public", recordedPublicKey)) {
        error = "not a 21kb release signing key file";
        return false;
    }
    Ed25519KeyPairFromSeed(out.seed, out.secretKey, out.publicKey);
    if (!ConstantTimeEqual(out.publicKey, recordedPublicKey)) {
        out = ReleaseSigningKey{};
        error = "release signing key file is damaged: its public key does not belong to its seed";
        return false;
    }
    return true;
}

std::string EncodePackContentKey(const AeadKey& key) {
    std::string text{ kContentKeyHeader };
    text += "\nkey ";
    text += ToHex(key.Span());
    text += '\n';
    return text;
}

bool DecodePackContentKey(std::string_view text, AeadKey& out, std::string& error) {
    const std::vector<std::string_view> lines = Lines(text);
    if (lines.size() != 2U || lines[0] != kContentKeyHeader || !ReadHexField(lines[1], "key", out.Span())) {
        error = "not a 21kb pack content key file";
        return false;
    }
    return true;
}

std::vector<std::uint8_t> EncodeTrustAnchor(const TrustAnchor& anchor) {
    std::vector<std::uint8_t> bytes(kAnchorMagic.begin(), kAnchorMagic.end());
    PutUInt32(bytes, kAnchorVersion);
    PutUInt32(bytes, anchor.packContentKey.has_value() ? kAnchorHasContentKey : 0U);
    bytes.insert(bytes.end(), anchor.releaseKey.begin(), anchor.releaseKey.end());
    if (anchor.packContentKey.has_value()) {
        bytes.insert(bytes.end(), anchor.packContentKey->data(), anchor.packContentKey->data() + kAeadKeyBytes);
    } else {
        bytes.insert(bytes.end(), kAeadKeyBytes, 0U);
    }
    bytes.push_back(static_cast<std::uint8_t>(anchor.productId.size() & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((anchor.productId.size() >> 8U) & 0xFFU));
    bytes.insert(bytes.end(), anchor.productId.begin(), anchor.productId.end());
    return bytes;
}

bool DecodeTrustAnchor(std::span<const std::uint8_t> bytes, TrustAnchor& out, std::string& error) {
    if (bytes.size() < kAnchorFixedBytes || !std::equal(kAnchorMagic.begin(), kAnchorMagic.end(), bytes.begin())) {
        error = "embedded trust anchor is not a 21kb trust anchor";
        return false;
    }
    if (GetUInt32(bytes, 8U) != kAnchorVersion) {
        error = "embedded trust anchor has an unsupported version";
        return false;
    }
    const std::uint32_t flags = GetUInt32(bytes, 12U);
    const std::size_t productIdBytes = static_cast<std::size_t>(bytes[80U]) | (static_cast<std::size_t>(bytes[81U]) << 8U);
    if ((flags & ~kAnchorKnownFlags) != 0U || bytes.size() != kAnchorFixedBytes + productIdBytes) {
        error = "embedded trust anchor is malformed";
        return false;
    }
    TrustAnchor anchor{};
    anchor.productId.assign(reinterpret_cast<const char*>(bytes.data() + kAnchorFixedBytes), productIdBytes);
    if (!IsValidProductId(anchor.productId)) {
        error = "embedded trust anchor names an invalid product id";
        return false;
    }
    std::copy_n(bytes.begin() + 16, kEd25519PublicKeyBytes, anchor.releaseKey.begin());
    if ((flags & kAnchorHasContentKey) != 0U) {
        anchor.packContentKey.emplace();
        std::copy_n(bytes.begin() + 48, kAeadKeyBytes, anchor.packContentKey->data());
    }
    out = std::move(anchor);
    return true;
}

TrustAnchorLookup LoadExecutableTrustAnchor() {
#if defined(_WIN32)
    return LookupResource(GetModuleHandleW(nullptr));
#else
    return TrustAnchorLookup{};
#endif
}

TrustAnchorLookup ReadTrustAnchorFromExecutable(const std::filesystem::path& executable) {
#if defined(_WIN32)
    HMODULE module = LoadLibraryExW(
        executable.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (module == nullptr) {
        TrustAnchorLookup lookup{};
        lookup.state = TrustAnchorLookup::State::Invalid;
        lookup.error = "executable could not be opened to read its trust anchor";
        return lookup;
    }
    TrustAnchorLookup lookup = LookupResource(module);
    FreeLibrary(module);
    return lookup;
#else
    static_cast<void>(executable);
    return TrustAnchorLookup{};
#endif
}

bool IsInsideProjectOrRepository(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path current = std::filesystem::absolute(path, error).lexically_normal().parent_path();
    while (!current.empty()) {
        for (const std::string_view marker : { std::string_view{ "Project.21kbproject" }, std::string_view{ ".git" },
                 std::string_view{ ".hg" }, std::string_view{ ".svn" } }) {
            std::error_code probeError;
            if (std::filesystem::exists(current / std::filesystem::path{ marker }, probeError)) {
                return true;
            }
        }
        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return false;
}

} // namespace kb::security
