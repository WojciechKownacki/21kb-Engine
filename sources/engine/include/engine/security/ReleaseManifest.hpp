#pragma once

#include "engine/security/Crypto.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::security {
struct ReleaseSigningKey;
}

// The signed list of everything a release ships.
//
// Packaging writes `release.kbmanifest` beside the player once every other file is final
// (after any Authenticode signature, which changes the executable's bytes). It names the
// product, a content version, a release number that only ever grows, and every shipped file
// with its size and SHA-512; for each asset pack it also records the digest the pack's own seal
// signs, so the player can bind a mounted pack to this release without hashing it twice. The
// whole text is signed with the release signing key, whose public half the player carries in
// its trust anchor.
//
// A packaged player verifies the manifest at startup and refuses to run with a modified,
// missing or unlisted file of the CRITICAL set -- executables, native modules and asset packs.
// The executable is hashed at startup; a native module is hashed as the exact bytes about to be
// loaded (EngineModuleLoader); an asset pack is bound through its seal at mount. Other files
// (licenses, notices) are covered by `kb_cli release verify`, which hashes everything.
//
// ANTI-ROLLBACK is optional and off by default. When a release is packaged with it, the player
// remembers the highest release number it has run for the product in per-user storage and
// refuses an older one. That stops a player from being downgraded to a release with a known
// flaw, at the cost of also refusing a deliberate reinstall of an older build; a user who
// deletes the per-user state resets it, so it is a policy aid, not a guarantee.
namespace kb::security {

inline constexpr std::string_view kReleaseManifestFileName = "release.kbmanifest";
inline constexpr std::uintmax_t kMaxReleaseManifestBytes = 16U * 1024U * 1024U;

struct ReleaseManifestFile {
    // Relative, '/'-separated, normalised.
    std::string path;
    std::uint64_t size = 0U;
    Sha512Digest sha512{};
};

struct ReleaseManifestPackSeal {
    std::string path;
    Sha512Digest sealDigest{};
};

struct ReleaseManifest {
    std::string productId;
    std::string contentVersion;
    std::uint64_t releaseNumber = 0U;
    bool antiRollback = false;
    // Sorted by path.
    std::vector<ReleaseManifestFile> files;
    std::vector<ReleaseManifestPackSeal> packs;
    Ed25519Signature signature{};

    [[nodiscard]] const ReleaseManifestFile* FindFile(std::string_view path) const noexcept;
    [[nodiscard]] const ReleaseManifestPackSeal* FindPack(std::string_view path) const noexcept;
};

enum class ReleaseManifestStatus : std::uint8_t {
    Success,
    Missing,
    Malformed,
    SignatureInvalid,
    ProductMismatch,
    FileMissing,
    FileModified,
    UnlistedFile,
    RolledBack,
    StateUnavailable,
};

[[nodiscard]] std::string_view ToString(ReleaseManifestStatus status) noexcept;

// A content version is 1..64 characters of [A-Za-z0-9._+-].
[[nodiscard]] bool IsValidContentVersion(std::string_view version) noexcept;

// Executables, native modules and asset packs: the files a player refuses to run with unless
// the manifest lists them with matching contents.
[[nodiscard]] bool IsCriticalReleaseFile(const std::filesystem::path& relativePath);

// Streams a file through SHA-512. False if it cannot be read.
[[nodiscard]] bool HashFileSha512(const std::filesystem::path& path, Sha512Digest& digest, std::uint64_t& size);

// Hashes every regular file under `root` except the manifest itself and records the seal digest
// of every asset pack. Fails on a symbolic link or a name that cannot be written portably.
[[nodiscard]] bool BuildReleaseManifest(
    const std::filesystem::path& root,
    ReleaseManifest& manifest,
    std::string& error);

// Signs and serialises; the result is the complete file.
[[nodiscard]] std::string SignReleaseManifest(ReleaseManifest& manifest, const ReleaseSigningKey& key);

// Parses the text and checks its signature against `releaseKey`; nothing in a manifest whose
// signature fails is looked at.
[[nodiscard]] ReleaseManifestStatus ParseAndVerifyReleaseManifest(
    std::string_view text,
    const Ed25519PublicKey& releaseKey,
    ReleaseManifest& out);

struct ReleaseVerification {
    ReleaseManifestStatus status = ReleaseManifestStatus::Missing;
    ReleaseManifest manifest;
    // The file that failed, when there is one.
    std::string detail;
    // Files present but not listed that are outside the critical set (for example packaging
    // receipts written after the manifest). Reported, not refused.
    std::vector<std::string> uncovered;
};

// Full check for tooling: every listed file must exist with the recorded size and SHA-512, every
// pack must still carry the seal the manifest names, and no critical file may be unlisted.
[[nodiscard]] ReleaseVerification VerifyReleaseDirectory(
    const std::filesystem::path& root,
    const Ed25519PublicKey& releaseKey,
    std::string_view expectedProductId);

// Startup check for a packaged player whose files live under `root`: the manifest verifies and
// names `expectedProductId`; every critical file under `root` is listed; every listed critical
// file exists with its recorded size; the files in `hashNow` (the running executable) also
// match their SHA-512.
[[nodiscard]] ReleaseVerification VerifyInstalledRelease(
    const std::filesystem::path& root,
    const Ed25519PublicKey& releaseKey,
    std::string_view expectedProductId,
    std::span<const std::filesystem::path> hashNow);

// Anti-rollback state: the highest release number seen for a product, kept in `stateRoot`.
// Refuses a lower number, records a higher one. Only called for a manifest that asks for it.
[[nodiscard]] ReleaseManifestStatus EnforceReleaseAntiRollback(
    const std::filesystem::path& stateRoot,
    const ReleaseManifest& manifest);

// The verified release this process runs from, installed once by a packaged player after
// VerifyInstalledRelease succeeds. Native module loading consults it: with a release installed
// only listed modules with matching bytes load; without one (editor, development player) a
// module loads with a warning.
struct InstalledRelease {
    std::filesystem::path root;
    ReleaseManifest manifest;
};

// Installing nullptr returns the process to development behaviour.
void InstallVerifiedRelease(std::shared_ptr<const InstalledRelease> release);
[[nodiscard]] std::shared_ptr<const InstalledRelease> CurrentVerifiedRelease();

} // namespace kb::security
