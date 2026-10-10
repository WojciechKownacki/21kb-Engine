#include "engine/security/ReleaseKeys.hpp"
#include "engine/project/ProjectManager.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#else
    #include <cstdlib>
#endif

namespace kb::security {
namespace {

constexpr std::string_view kSigningKeyHeader = "21kb-release-signing-key 1";
constexpr std::string_view kContentKeyHeader = "21kb-pack-content-key 1";
constexpr std::array<std::uint8_t, 8U> kAnchorMagic{ '2', '1', 'K', 'B', 'T', 'R', 'S', 'T' };
constexpr std::uint32_t kAnchorVersion = 1U;
constexpr std::string_view kInstallationHeader = "21kb-installation-secret 1";
constexpr std::string_view kSaveSecretSalt = "21KB-SAVE-SECRET-V1";
constexpr std::uint32_t kAnchorHasContentKey = 1U << 0U;
constexpr std::uint32_t kAnchorHasSaveSecret = 1U << 1U;
constexpr std::uint32_t kAnchorKnownFlags = kAnchorHasContentKey | kAnchorHasSaveSecret;
// magic, version, flags, release key, content key, save secret, product id length
constexpr std::size_t kAnchorReleaseKeyOffset = 16U;
constexpr std::size_t kAnchorContentKeyOffset = 48U;
constexpr std::size_t kAnchorSaveSecretOffset = 80U;
constexpr std::size_t kAnchorProductIdLengthOffset = 112U;
constexpr std::size_t kAnchorFixedBytes = kAnchorProductIdLengthOffset + 2U;
// Trust anchor slot: magic, anchor length, reserved, anchor.
constexpr std::size_t kSlotLengthOffset = 16U;
constexpr std::size_t kSlotReservedOffset = 20U;
constexpr std::size_t kSlotAnchorOffset = 24U;
static_assert(kTrustAnchorSlotMagic.size() == kSlotLengthOffset);

#if defined(__linux__) && !defined(__ANDROID__)
// The slot packaging fills in a Linux player. It is read through a volatile pointer: the compiler
// must not fold the empty slot it sees here into the code that reads it.
__attribute__((section(".kb_trust_anchor"), used, aligned(16)))
const unsigned char kExecutableTrustAnchorSlot[kTrustAnchorSlotBytes] = {
    '2', '1', 'K', 'B', '-', 'A', 'N', 'C', 'H', 'O', 'R', '-', 'S', 'L', 'O', 'T',
};
#endif

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

[[nodiscard]] std::uint16_t GetUInt16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1U] << 8U));
}

[[nodiscard]] std::uint64_t GetUInt64(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint64_t>(GetUInt32(bytes, offset)) |
        (static_cast<std::uint64_t>(GetUInt32(bytes, offset + 4U)) << 32U);
}

[[nodiscard]] TrustAnchorLookup InvalidLookup(std::string error) {
    TrustAnchorLookup lookup{};
    lookup.state = TrustAnchorLookup::State::Invalid;
    lookup.error = std::move(error);
    return lookup;
}

[[nodiscard]] bool IsElfImage(std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= 4U && bytes[0] == 0x7FU && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F';
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

GameSaveSecret DeriveGameSaveSecret(const ReleaseSigningKey& key, std::string_view productId) {
    GameSaveSecret secret;
    HkdfSha512(
        secret.Span(),
        key.seed.Span(),
        std::span{ reinterpret_cast<const std::uint8_t*>(kSaveSecretSalt.data()), kSaveSecretSalt.size() },
        std::span{ reinterpret_cast<const std::uint8_t*>(productId.data()), productId.size() });
    return secret;
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
    PutUInt32(bytes, (anchor.packContentKey.has_value() ? kAnchorHasContentKey : 0U) |
            (anchor.saveSecret.has_value() ? kAnchorHasSaveSecret : 0U));
    bytes.insert(bytes.end(), anchor.releaseKey.begin(), anchor.releaseKey.end());
    if (anchor.packContentKey.has_value()) {
        bytes.insert(bytes.end(), anchor.packContentKey->data(), anchor.packContentKey->data() + kAeadKeyBytes);
    } else {
        bytes.insert(bytes.end(), kAeadKeyBytes, 0U);
    }
    if (anchor.saveSecret.has_value()) {
        bytes.insert(bytes.end(), anchor.saveSecret->data(), anchor.saveSecret->data() + GameSaveSecret::size());
    } else {
        bytes.insert(bytes.end(), GameSaveSecret::size(), 0U);
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
    const std::size_t productIdBytes = static_cast<std::size_t>(bytes[kAnchorProductIdLengthOffset]) |
        (static_cast<std::size_t>(bytes[kAnchorProductIdLengthOffset + 1U]) << 8U);
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
    std::copy_n(bytes.begin() + kAnchorReleaseKeyOffset, kEd25519PublicKeyBytes, anchor.releaseKey.begin());
    if ((flags & kAnchorHasContentKey) != 0U) {
        anchor.packContentKey.emplace();
        std::copy_n(bytes.begin() + kAnchorContentKeyOffset, kAeadKeyBytes, anchor.packContentKey->data());
    }
    if ((flags & kAnchorHasSaveSecret) != 0U) {
        anchor.saveSecret.emplace();
        std::copy_n(bytes.begin() + kAnchorSaveSecretOffset, GameSaveSecret::size(), anchor.saveSecret->data());
    }
    out = std::move(anchor);
    return true;
}

TrustAnchorLookup DecodeTrustAnchorSlot(std::span<const std::uint8_t> slot) {
    if (slot.size() != kTrustAnchorSlotBytes ||
        !std::equal(kTrustAnchorSlotMagic.begin(), kTrustAnchorSlotMagic.end(), slot.begin(),
            [](char expected, std::uint8_t actual) { return static_cast<std::uint8_t>(expected) == actual; })) {
        return InvalidLookup("the trust anchor slot is not a 21kb trust anchor slot");
    }
    const std::uint32_t length = GetUInt32(slot, kSlotLengthOffset);
    if (GetUInt32(slot, kSlotReservedOffset) != 0U || length > kTrustAnchorSlotBytes - kSlotAnchorOffset ||
        std::any_of(slot.begin() + static_cast<std::ptrdiff_t>(kSlotAnchorOffset + length), slot.end(),
            [](std::uint8_t value) { return value != 0U; })) {
        return InvalidLookup("the trust anchor slot is malformed");
    }
    if (length == 0U) {
        return TrustAnchorLookup{};
    }
    TrustAnchorLookup lookup{};
    lookup.state = TrustAnchorLookup::State::Invalid;
    if (DecodeTrustAnchor(slot.subspan(kSlotAnchorOffset, length), lookup.anchor, lookup.error)) {
        lookup.state = TrustAnchorLookup::State::Present;
    }
    return lookup;
}

TrustAnchorLookup ReadTrustAnchorFromElf(std::span<const std::uint8_t> image) {
    // ELF64 little-endian: e_shoff at 0x28, e_shentsize at 0x3A, e_shnum at 0x3C, e_shstrndx at 0x3E;
    // a section header holds sh_name at 0x00, sh_type at 0x04, sh_offset at 0x18, sh_size at 0x20.
    constexpr std::size_t kHeaderBytes = 64U;
    constexpr std::size_t kSectionHeaderBytes = 64U;
    constexpr std::uint32_t kSectionNoBits = 8U;
    constexpr std::uint16_t kExtendedIndex = 0xFFFFU;
    if (!IsElfImage(image) || image.size() < kHeaderBytes) {
        return InvalidLookup("the executable is not an ELF image");
    }
    if (image[4] != 2U || image[5] != 1U) {
        return InvalidLookup("the executable is not a 64-bit little-endian ELF image");
    }
    const std::uint64_t sectionTable = GetUInt64(image, 0x28U);
    if (sectionTable == 0U) {
        return TrustAnchorLookup{};
    }
    if (GetUInt16(image, 0x3AU) != kSectionHeaderBytes || sectionTable > image.size() ||
        image.size() - sectionTable < kSectionHeaderBytes) {
        return InvalidLookup("the executable's ELF section table is malformed");
    }
    const auto section = [&](std::uint64_t index) {
        return image.subspan(static_cast<std::size_t>(sectionTable + index * kSectionHeaderBytes), kSectionHeaderBytes);
    };
    // Section 0 carries the real count and string table index when they do not fit the header.
    std::uint64_t sectionCount = GetUInt16(image, 0x3CU);
    if (sectionCount == 0U) {
        sectionCount = GetUInt64(section(0U), 0x20U);
    }
    std::uint64_t namesIndex = GetUInt16(image, 0x3EU);
    if (namesIndex == kExtendedIndex) {
        namesIndex = GetUInt32(section(0U), 0x28U);
    }
    if (sectionCount > (image.size() - sectionTable) / kSectionHeaderBytes || namesIndex >= sectionCount) {
        return InvalidLookup("the executable's ELF section table is malformed");
    }
    const std::span<const std::uint8_t> namesHeader = section(namesIndex);
    const std::uint64_t namesOffset = GetUInt64(namesHeader, 0x18U);
    const std::uint64_t namesSize = GetUInt64(namesHeader, 0x20U);
    if (namesOffset > image.size() || namesSize > image.size() - namesOffset) {
        return InvalidLookup("the executable's ELF section names are malformed");
    }
    const std::span<const std::uint8_t> names = image.subspan(static_cast<std::size_t>(namesOffset), static_cast<std::size_t>(namesSize));
    std::optional<std::span<const std::uint8_t>> slot;
    for (std::uint64_t index = 1U; index < sectionCount; ++index) {
        const std::span<const std::uint8_t> header = section(index);
        const std::uint32_t nameOffset = GetUInt32(header, 0x00U);
        if (nameOffset >= names.size()) {
            return InvalidLookup("the executable's ELF section names are malformed");
        }
        const auto* const nameBegin = reinterpret_cast<const char*>(names.data() + nameOffset);
        const std::size_t nameLength = strnlen(nameBegin, names.size() - nameOffset);
        if (std::string_view{ nameBegin, nameLength } != kTrustAnchorElfSectionName) {
            continue;
        }
        const std::uint64_t offset = GetUInt64(header, 0x18U);
        const std::uint64_t size = GetUInt64(header, 0x20U);
        if (slot.has_value() || GetUInt32(header, 0x04U) == kSectionNoBits || offset > image.size() ||
            size > image.size() - offset) {
            return InvalidLookup("the executable's trust anchor section is malformed");
        }
        slot = image.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
    }
    return slot.has_value() ? DecodeTrustAnchorSlot(*slot) : TrustAnchorLookup{};
}

TrustAnchorLookup LoadExecutableTrustAnchor() {
#if defined(_WIN32)
    return LookupResource(GetModuleHandleW(nullptr));
#elif defined(__linux__) && !defined(__ANDROID__)
    std::array<std::uint8_t, kTrustAnchorSlotBytes> slot{};
    const volatile unsigned char* const source = kExecutableTrustAnchorSlot;
    for (std::size_t index = 0U; index < slot.size(); ++index) {
        slot[index] = source[index];
    }
    return DecodeTrustAnchorSlot(slot);
#else
    return TrustAnchorLookup{};
#endif
}

TrustAnchorLookup ReadTrustAnchorFromExecutable(const std::filesystem::path& executable) {
    {
        std::ifstream input{ executable, std::ios::binary };
        std::array<char, 4U> magic{};
        input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (input.gcount() == 4 && magic[0] == '\x7F' && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
            input.seekg(0, std::ios::end);
            const std::streamoff size = input.tellg();
            constexpr std::streamoff kMaximumImageBytes = std::streamoff{ 1 } << 32;
            if (size <= 0 || size > kMaximumImageBytes) {
                return InvalidLookup("executable could not be read to find its trust anchor");
            }
            std::vector<std::uint8_t> image(static_cast<std::size_t>(size));
            input.seekg(0, std::ios::beg);
            input.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
            if (!input) {
                return InvalidLookup("executable could not be read to find its trust anchor");
            }
            return ReadTrustAnchorFromElf(image);
        }
    }
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

TrustAnchorLookup FindReleaseTrustAnchor(const std::filesystem::path& releaseDirectory) {
    std::error_code error;
    std::vector<std::filesystem::path> players;
    for (std::filesystem::directory_iterator iterator{ releaseDirectory, error }, end; !error && iterator != end;
         iterator.increment(error)) {
        std::error_code statusError;
        if (!iterator->is_regular_file(statusError) || statusError) {
            continue;
        }
        // A Windows .exe, or a Linux ELF player (which has no extension).
        bool player = iterator->path().extension() == ".exe";
        if (!player) {
            std::ifstream input{ iterator->path(), std::ios::binary };
            std::array<char, 4U> magic{};
            input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            player = input.gcount() == 4 && magic[0] == '\x7F' && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
        }
        if (player) {
            players.push_back(iterator->path());
        }
    }
    if (error) {
        return InvalidLookup("release directory could not be listed");
    }
    // Directory order is the file system's; the result must not depend on it.
    std::ranges::sort(players);
    for (const std::filesystem::path& player : players) {
        TrustAnchorLookup lookup = ReadTrustAnchorFromExecutable(player);
        if (lookup.state == TrustAnchorLookup::State::Invalid) {
            lookup.error = player.filename().string() + ": " + lookup.error;
        }
        if (lookup.state != TrustAnchorLookup::State::Absent) {
            return lookup;
        }
    }
    return TrustAnchorLookup{};
}

std::filesystem::path DefaultUserSecurityRoot(std::string_view productId) {
    if (!IsValidProductId(productId)) {
        return {};
    }
#if defined(_WIN32)
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0U);
    if (required <= 1U) {
        return {};
    }
    std::wstring buffer(required, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required);
    if (length == 0U || length >= required) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path{ buffer } / "21kb" / std::filesystem::path{ productId };
#else
    if (const char* data = std::getenv("XDG_DATA_HOME"); data != nullptr && data[0] == '/') {
        return std::filesystem::path{ data } / "21kb" / std::filesystem::path{ productId };
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] == '/') {
        return std::filesystem::path{ home } / ".local" / "share" / "21kb" / std::filesystem::path{ productId };
    }
    return {};
#endif
}

bool LoadOrCreateInstallationSecret(const std::filesystem::path& root, InstallationSecret& out, std::string& error) {
    if (root.empty()) {
        error = "no per-user storage is available for the installation secret";
        return false;
    }
    const std::filesystem::path path = root / "installation.secret";
    std::error_code fileError;
    if (std::filesystem::exists(path, fileError)) {
        std::ifstream input{ path, std::ios::binary };
        std::string text{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        const std::vector<std::string_view> lines = Lines(text);
        const bool decoded = lines.size() == 2U && lines[0] == kInstallationHeader && ReadHexField(lines[1], "secret", out.Span());
        SecureWipe(std::span{ reinterpret_cast<std::uint8_t*>(text.data()), text.size() });
        if (!decoded) {
            error = "the installation secret is damaged: " + path.string();
        }
        return decoded;
    }
    if (!SecureRandom(out.Span())) {
        error = "the system random number generator is unavailable";
        return false;
    }
    std::filesystem::create_directories(root, fileError);
    std::string text = std::string{ kInstallationHeader } + "\nsecret " + ToHex(out.Span()) + '\n';
    const std::filesystem::path temporary = root / "installation.secret.tmp";
    bool written = false;
    {
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        output << text;
        written = output.good();
    }
    SecureWipe(std::span{ reinterpret_cast<std::uint8_t*>(text.data()), text.size() });
#if !defined(_WIN32)
    std::filesystem::permissions(temporary, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, fileError);
#endif
    if (written) {
        std::filesystem::rename(temporary, path, fileError);
        written = !fileError;
    }
    if (!written) {
        error = "the installation secret could not be written to " + root.string();
    }
    return written;
}

bool IsInsideProjectOrRepository(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path current = std::filesystem::absolute(path, error).lexically_normal().parent_path();
    while (!current.empty()) {
        if (kb::project::ProjectManager::IsProjectDirectory(current)) {
            return true;
        }
        for (const std::string_view marker : { std::string_view{ ".git" },
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
