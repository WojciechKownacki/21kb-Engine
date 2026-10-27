#pragma once

#include "engine/mods/ModVersion.hpp"
#include "engine/security/Crypto.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The MANIFEST of a mod: `Mod.kbmod`, a small text file that says what the mod is, which engine
// interface and which other mods it needs, and which pack holds its content.
//
//     21kb-mod 1
//     id acme.crates
//     name Crate Pack
//     version 1.2.0
//     author Acme
//     description More crates for the warehouse.
//     api ^1.0.0
//     requires acme.core >=1.0.0 <2.0.0
//     conflicts acme.oldcrates
//     entry /Game/Mods/Crates/Main.21kbscene
//     pack Crates.kbpack
//     pack-sha512 <128 hexadecimal digits>
//     signer <64 hexadecimal digits>
//
// A mod is CONTENT, not code: assets, scenes and Lua scripts in a pack (a chunk pack, cooked against
// the game build it extends) that the game mounts beside its own. The manifest cannot name a native
// library, a plugin or an executable; keys that try to are refused by name, so a mod that needs
// native code is not a mod this engine will load. Scripts of a mod run in the same Lua sandbox as the
// game's own scripts.
//
// The manifest binds itself to its pack by the pack's SHA-512 and, for a signed pack, to the key
// that sealed it. The manifest is metadata for choosing and ordering mods; what a mod may do is
// decided by the pack's seal and the player's trust policy (ModCatalog).
namespace kb::mods {

inline constexpr std::string_view kModManifestFileName = "Mod.kbmod";
inline constexpr std::string_view kModManifestHeader = "21kb-mod";
inline constexpr std::uint32_t kModManifestVersion = 1U;
inline constexpr std::uintmax_t kMaxModManifestBytes = 64U * 1024U;
inline constexpr std::size_t kMaxModIdBytes = 48U;
inline constexpr std::size_t kMaxModDependencies = 64U;

struct ModDependency {
    std::string id;
    ModVersionRange range;

    [[nodiscard]] bool operator==(const ModDependency&) const noexcept = default;
};

struct ModManifest {
    // Lower case letters, digits and '.', '_', '-': also the label of the mod's pack.
    std::string id;
    std::string name;
    ModVersion version{};
    std::string author;
    std::string description;
    // The mod interface versions this mod was written for.
    ModVersionRange api;
    std::vector<ModDependency> dependencies;
    std::vector<std::string> conflicts;
    // A scene the game may open for this mod (a virtual path), or empty.
    std::string entryScene;
    // The pack's file name beside the manifest, and its SHA-512.
    std::string pack;
    kb::security::Sha512Digest packSha512{};
    // The key the pack is sealed with, when the mod is signed.
    std::optional<kb::security::Ed25519PublicKey> signer;

    [[nodiscard]] bool operator==(const ModManifest&) const noexcept = default;
};

enum class ModManifestStatus : std::uint8_t {
    Success,
    TooLarge,
    // The first line is not "21kb-mod <version>".
    NotAManifest,
    UnsupportedVersion,
    // A line that is not "key value".
    Malformed,
    UnknownKey,
    DuplicateKey,
    MissingField,
    InvalidValue,
    // The manifest names a native library, plugin or executable.
    NativeCodeRefused,
};

[[nodiscard]] std::string_view ToString(ModManifestStatus status) noexcept;

struct ModManifestResult {
    ModManifestStatus status = ModManifestStatus::Success;
    // 1-based line of the problem, 0 when it concerns the whole file.
    std::uint32_t line = 0U;
    std::string detail;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == ModManifestStatus::Success;
    }
    [[nodiscard]] std::string Describe() const;
};

[[nodiscard]] bool IsValidModId(std::string_view id) noexcept;
[[nodiscard]] bool IsValidModPackFileName(std::string_view name) noexcept;

// Strict parse; `out` changes only on Success.
[[nodiscard]] ModManifestResult ParseModManifest(std::string_view text, ModManifest& out);
// The canonical text of a manifest that ParseModManifest would accept; empty for one it would refuse.
[[nodiscard]] std::string EncodeModManifest(const ModManifest& manifest);
// Reads and parses the file.
[[nodiscard]] ModManifestResult ReadModManifest(const std::filesystem::path& path, ModManifest& out);

} // namespace kb::mods
