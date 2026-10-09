#pragma once

#include "engine/security/Crypto.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Key material of a shipped game, and the one place its on-disk forms are defined.
//
// A RELEASE SIGNING KEY is an Ed25519 key pair owned by whoever publishes the game. Its secret
// half lives in a key file outside every project (kb_cli keys generate refuses to write one into
// a project or a repository) and never ships. Packaging signs the asset packs and the release
// manifest with it.
//
// The TRUST ANCHOR is the public half plus the few values a shipped player needs to check what
// it loads. Packaging embeds it into the player itself -- an RT_RCDATA resource on Windows
// (written before any Authenticode signature, so replacing it breaks that signature), the
// reserved section of a Linux ELF player, an asset inside the signed APK on Android -- and a
// player that carries one runs in PACKAGED mode: it refuses unsigned or foreign content instead
// of warning about it.
namespace kb::security {

// Resource id of the trust anchor inside a Windows player (resource type RT_RCDATA). Must match
// TRUST_ANCHOR_RESOURCE_ID in scripts/windows_pe_resources.py, which writes it.
inline constexpr std::uint16_t kTrustAnchorResourceId = 2101U;
// Name of the trust anchor among the assets of an Android package.
inline constexpr std::string_view kTrustAnchorAssetName = "kb_trust_anchor.bin";

// A Linux player reserves its trust anchor slot at build time: an allocated, read-only ELF section
// of this name and exactly kTrustAnchorSlotBytes, which packaging fills in place after the build
// (scripts/elf_trust_anchor.py; its SECTION_NAME, SLOT_BYTES and SLOT_MAGIC must match these).
// The slot is the 16-byte magic, the anchor's length as a little-endian u32 (0 while the slot is
// empty), four zero bytes, the encoded anchor and zero padding.
inline constexpr std::string_view kTrustAnchorElfSectionName = ".kb_trust_anchor";
inline constexpr std::size_t kTrustAnchorSlotBytes = 1024U;
inline constexpr std::string_view kTrustAnchorSlotMagic = "21KB-ANCHOR-SLOT";

inline constexpr std::size_t kMaxProductIdBytes = 128U;

// 1..128 characters of [A-Za-z0-9._-], starting with a letter or digit. A product id names the
// game in release manifests and in per-user state, so it must be a portable file name.
[[nodiscard]] bool IsValidProductId(std::string_view productId) noexcept;

struct ReleaseSigningKey {
    Ed25519Seed seed;
    Ed25519SecretKey secretKey;
    Ed25519PublicKey publicKey{};
};

[[nodiscard]] bool GenerateReleaseSigningKey(ReleaseSigningKey& out) noexcept;
// Text form of a key file: a versioned header line, the seed and the public key in hex. The
// public key is redundant and is checked against the seed on load, so a damaged file is refused
// rather than silently signing with a different key.
[[nodiscard]] std::string EncodeReleaseSigningKey(const ReleaseSigningKey& key);
[[nodiscard]] bool DecodeReleaseSigningKey(std::string_view text, ReleaseSigningKey& out, std::string& error);

// Symmetric key that encrypts the blocks of an asset pack. Unlike the signing key it has to ship
// with the game (inside the trust anchor) so the player can decrypt; it keeps content from being
// lifted out of a pack with ordinary tools, it cannot keep it from a determined attacker.
[[nodiscard]] std::string EncodePackContentKey(const AeadKey& key);
[[nodiscard]] bool DecodePackContentKey(std::string_view text, AeadKey& out, std::string& error);

using GameSaveSecret = SecretBytes<32U>;
using InstallationSecret = SecretBytes<32U>;

// The per-game secret save files are authenticated with. Derived from the signing key and the
// product id, so it stays the same for every release signed with the same key -- saves survive
// updates -- and nobody without the key can compute it.
[[nodiscard]] GameSaveSecret DeriveGameSaveSecret(const ReleaseSigningKey& key, std::string_view productId);

struct TrustAnchor {
    std::string productId;
    Ed25519PublicKey releaseKey{};
    std::optional<AeadKey> packContentKey;
    std::optional<GameSaveSecret> saveSecret;
};

[[nodiscard]] std::vector<std::uint8_t> EncodeTrustAnchor(const TrustAnchor& anchor);
[[nodiscard]] bool DecodeTrustAnchor(std::span<const std::uint8_t> bytes, TrustAnchor& out, std::string& error);

struct TrustAnchorLookup {
    enum class State : std::uint8_t {
        // No anchor: a development player, or a platform that cannot carry one.
        Absent,
        Present,
        // An anchor is there but does not decode. Never treated as Absent -- that would turn a
        // damaged shipped player into a permissive development one.
        Invalid,
    };
    State state = State::Absent;
    TrustAnchor anchor;
    std::string error;
};

// The trust anchor embedded in the running executable. Windows reads its RT_RCDATA resource and
// Linux the slot section linked into the player; other platforms report Absent (Android hosts
// read their anchor asset and decode it with DecodeTrustAnchor).
[[nodiscard]] TrustAnchorLookup LoadExecutableTrustAnchor();

// The trust anchor embedded in another executable on disk, for release tooling: a Linux ELF
// player on every host, a Windows PE player on Windows.
[[nodiscard]] TrustAnchorLookup ReadTrustAnchorFromExecutable(const std::filesystem::path& executable);

// The trust anchor of the player a release directory ships: the first file directly in the
// directory that is a player (a .exe, or an ELF image) and carries one. Absent when no player
// does, Invalid when a player's anchor is damaged.
[[nodiscard]] TrustAnchorLookup FindReleaseTrustAnchor(const std::filesystem::path& releaseDirectory);

// A trust anchor slot (see kTrustAnchorElfSectionName): Absent while empty, Invalid when it is
// not a well-formed slot or its anchor does not decode.
[[nodiscard]] TrustAnchorLookup DecodeTrustAnchorSlot(std::span<const std::uint8_t> slot);

// The trust anchor slot of a 64-bit little-endian ELF image. Absent when the image has no slot
// section, Invalid when the image or the slot is malformed.
[[nodiscard]] TrustAnchorLookup ReadTrustAnchorFromElf(std::span<const std::uint8_t> image);

// Per-user directory a packaged player keeps its security state in (installation secret,
// anti-rollback record): %LOCALAPPDATA%\21kb\<product> on Windows, $XDG_DATA_HOME/21kb/<product>
// (or ~/.local/share/21kb/<product>) elsewhere. Empty when it cannot be determined.
[[nodiscard]] std::filesystem::path DefaultUserSecurityRoot(std::string_view productId);

// The random secret that binds saves to one installation, kept in `root`. Created on first use.
[[nodiscard]] bool LoadOrCreateInstallationSecret(
    const std::filesystem::path& root,
    InstallationSecret& out,
    std::string& error);

// True when `path` lies inside a directory that holds a 21kb project or a version-control
// working tree; private keys must never be written there.
[[nodiscard]] bool IsInsideProjectOrRepository(const std::filesystem::path& path);

} // namespace kb::security
