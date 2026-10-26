#pragma once

#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/mods/ModManifest.hpp"
#include "engine/security/Crypto.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// The mods a game has INSTALLED, and which of them it may load.
//
//     <Mods>/Mods.state     21kb-mod-state 1, then "enabled <id>" lines: the enabled mods, in load order
//     <Mods>/Mods.trust     21kb-mod-trust 1, then "allow-unsigned" and "signer <64 hex digits> [label]" lines
//     <Mods>/<id>/Mod.kbmod the manifest (ModManifest.hpp)
//     <Mods>/<id>/<pack>    the mod's chunk pack
//
// WHAT MAKES A MOD LOADABLE. Nothing in a mod is trusted because the mod says so. A mod loads when
//   - its manifest parses and names no native code, and its folder is its id;
//   - its pack is the one the manifest hashes (SHA-512) and mounts as a CHUNK pack labelled with the
//     mod's id, cooked against exactly this build of the game (the base pack's catalogue identity),
//     for an interface version the manifest accepts;
//   - its pack is sealed by a key the player trusts (Mods.trust), or, only when the player allows
//     it, not sealed at all. A seal proves the content is what its author sealed; the trust list
//     says whose seal counts. The key in the manifest, when there is one, must be the key of the seal;
//   - every mod it requires is installed, enabled, loadable and in the version range it asks for, and
//     no loadable mod ahead of it in load order conflicts with it.
// A mod that fails any of these is REFUSED with a reason and the others still load.
//
// WHAT A LOADED MOD CAN DO. It adds assets (scenes, scripts, data, textures, audio...) beside the
// game's: a chunk pack cannot replace an asset the game or another mod has, and it cannot change
// project settings. Its scripts run in the same Lua sandbox as the game's own scripts. It can not
// bring native code: the manifest has no way to name any, and the pack format carries none. The game
// decides whether and how a mod's content is used (the `Mods` script functions list what loaded).
namespace kb::mods {

inline constexpr std::string_view kModStateFileName = "Mods.state";
inline constexpr std::string_view kModTrustFileName = "Mods.trust";
inline constexpr std::size_t kMaxInstalledMods = 256U;

struct ModTrustPolicy {
    // Whether a mod whose pack is not sealed may load. A shipped game leaves this off.
    bool allowUnsigned = false;
    // Keys whose seals the player trusts for mods.
    std::vector<kb::security::Ed25519PublicKey> signers;

    [[nodiscard]] bool Trusts(const kb::security::Ed25519PublicKey& key) const noexcept;
    [[nodiscard]] bool operator==(const ModTrustPolicy&) const noexcept = default;
};

[[nodiscard]] bool ParseModTrustPolicy(std::string_view text, ModTrustPolicy& out, std::string& error);
[[nodiscard]] std::string EncodeModTrustPolicy(const ModTrustPolicy& policy);

// What the game offers a mod.
struct ModHost {
    ModVersion apiVersion = kModApiVersion;
    // The catalogue identity of the game's base pack. A mod's pack names the base it was cooked
    // against; when this is set a mod built for another build of the game is refused. Unset skips
    // the check (an offline inspection that has no game to compare with).
    std::optional<kb::assets::bake::AssetBakeDigest> baseIdentity;
};

enum class ModProblem : std::uint8_t {
    None,
    // Mod.kbmod is missing, does not parse or names native code.
    ManifestInvalid,
    // The folder is not named after the mod's id.
    FolderMismatch,
    PackMissing,
    PackHashMismatch,
    // The pack does not mount: damaged, tampered, encrypted or sealed with a broken seal.
    PackRejected,
    // The pack is not a chunk pack labelled with the mod's id.
    PackNotAModChunk,
    // The manifest names a signer other than the key that sealed the pack.
    SignerMismatch,
    Unsigned,
    UntrustedSigner,
    ApiIncompatible,
    // Cooked against another build of the game.
    BaseMismatch,
    DependencyMissing,
    DependencyDisabled,
    DependencyVersion,
    // A mod it requires cannot load.
    DependencyRefused,
    DependencyCycle,
    Conflict,
    // The runtime refused the pack when mounting it (see ModMountReport).
    MountRefused,
    // Installed but not enabled: not a failure.
    Disabled,
};

[[nodiscard]] std::string_view ToString(ModProblem problem) noexcept;

struct ModInfo {
    std::filesystem::path directory;
    ModManifest manifest;
    bool enabled = false;
    ModProblem problem = ModProblem::None;
    std::string detail;
    // The pack carries a valid seal, and the key that made it.
    bool sealed = false;
    std::optional<kb::security::Ed25519PublicKey> sealSigner;
    std::filesystem::path packPath;

    [[nodiscard]] bool Loadable() const noexcept {
        return enabled && problem == ModProblem::None;
    }
    [[nodiscard]] std::string Describe() const;
};

struct ModPlan {
    // The mods to mount, dependencies first, otherwise in the order they were enabled.
    std::vector<ModInfo> load;
    // Enabled mods that cannot load, with the reason.
    std::vector<ModInfo> refused;
    // Installed and not enabled.
    std::vector<ModInfo> disabled;

    [[nodiscard]] const ModInfo* FindLoad(std::string_view id) const noexcept;
};

// Checks one mod folder on its own: everything above except what depends on other mods. This is
// what an install runs, and the first step of building a plan. `info.enabled` is not set.
[[nodiscard]] ModInfo InspectMod(
    const std::filesystem::path& directory,
    const ModHost& host,
    const ModTrustPolicy& policy);

// Puts the pack named by `manifest.pack` (beside `manifest` in `directory`) into the manifest: its
// hash and, when the pack is sealed, the key. False, with a reason, for a missing or unsealed-unreadable pack.
[[nodiscard]] bool BindModManifestToPack(
    const std::filesystem::path& directory,
    ModManifest& manifest,
    std::string& error);

class ModCatalog {
public:
    explicit ModCatalog(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& Root() const noexcept {
        return root_;
    }

    // The installed mods in order: the enabled ones in load order, then the rest by id. Each is
    // inspected on its own; dependencies and conflicts are the plan's business.
    [[nodiscard]] std::vector<ModInfo> List(const ModHost& host, const ModTrustPolicy& policy) const;
    // The enabled mods in load order, as the state file names them.
    [[nodiscard]] std::vector<std::string> EnabledIds() const;

    // The plan for a game start. `excluded` are mods that must not load (with the reason shown),
    // used when mounting a mod's pack was refused.
    [[nodiscard]] ModPlan Resolve(
        const ModHost& host,
        const ModTrustPolicy& policy,
        const std::vector<std::pair<std::string, std::string>>& excluded = {}) const;

    // Copies the mod in `source` (a folder with Mod.kbmod and its pack) into the catalogue and
    // enables it. Refuses a mod that does not inspect clean, and one whose id is installed in the
    // same or a newer version unless `replace`. `installed` describes the result.
    [[nodiscard]] bool Install(
        const std::filesystem::path& source,
        const ModHost& host,
        const ModTrustPolicy& policy,
        bool replace,
        ModInfo& installed,
        std::string& error);
    [[nodiscard]] bool Remove(std::string_view id, std::string& error);
    // Enabling puts the mod last in load order; disabling keeps the others' order. Neither
    // checks dependencies: Resolve names what cannot load.
    [[nodiscard]] bool SetEnabled(std::string_view id, bool enabled, std::string& error);

    [[nodiscard]] ModTrustPolicy ReadTrustPolicy() const;
    [[nodiscard]] bool WriteTrustPolicy(const ModTrustPolicy& policy, std::string& error) const;

private:
    [[nodiscard]] bool WriteEnabled(const std::vector<std::string>& ids, std::string& error) const;

    std::filesystem::path root_;
};

} // namespace kb::mods
